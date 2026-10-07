#include "config/config_test.hpp"
#include "config/settings_store.hpp"
#include "encode/format.hpp"
#include "recorder/audio_source.hpp"
#include "recorder/settings.hpp"
#include "recorder/settings_changes.hpp"
#include "recorder/source.hpp"

#include <gtest/gtest.h>
#include <tge/testing/expected_log.hpp>

#include <cstddef>
#include <filesystem>
#include <string>
#include <sys/stat.h>

using namespace Klip;

namespace
{
using CSettingsStoreTest = Tests::CConfigTest;

constexpr Recorder::SSettingsChanges AllChanges{ .choices = true, .screenToken = true, .windowToken = true };
constexpr size_t                     System{ static_cast<size_t>(Recorder::EAudioSource::System) };

//////////////////////////////////////////////////////////////////////////
Recorder::SSettings MakeDefaults(std::filesystem::path const& home)
{
	Recorder::SSettings settings{};

	settings.directory = (home / "Videos" / "klip-captures").string();

	return settings;
}

//////////////////////////////////////////////////////////////////////////
Recorder::SSettings MakeChanged(std::filesystem::path const& home)
{
	Recorder::SSettings settings{ MakeDefaults(home) };

	settings.directory = "/srv/clips";
	settings.source = Recorder::ESource::Window;
	settings.codec = Encode::ECodec::Hevc;
	settings.maxFrameRate = 60;
	settings.audio[System] = { .enabled = true, .device = "sink", .gainDecibels = -6 };
	settings.screenToken = "screen-token";

	return settings;
}

//////////////////////////////////////////////////////////////////////////
ino_t GetInode(std::filesystem::path const& path)
{
	struct stat status{};

	EXPECT_EQ(::stat(path.c_str(), &status), 0) << path;

	return status.st_ino;
}
} // namespace

//////////////////////////////////////////////////////////////////////////
TEST_F(CSettingsStoreTest, MissingFileGivesDefaultsUnderVideos)
{
	Tge::Testing::CExpectedLog const expected{ "Config", 0, 0 };
	Config::CSettingsStore           store{};

	store.Initialize(m_configHome, m_home);

	EXPECT_EQ(store.Load(), MakeDefaults(m_home));
}

//////////////////////////////////////////////////////////////////////////
TEST_F(CSettingsStoreTest, LoadingWritesNothing)
{
	Config::CSettingsStore store{};

	store.Initialize(m_configHome, m_home);
	store.Load();

	EXPECT_FALSE(std::filesystem::exists(GetSettingsPath()));
}

//////////////////////////////////////////////////////////////////////////
TEST_F(CSettingsStoreTest, SavedSettingsLoadBack)
{
	Tge::Testing::CExpectedLog const expected{ "Config", 0, 0 };
	Config::CSettingsStore           writer{};
	Config::CSettingsStore           reader{};

	writer.Initialize(m_configHome, m_home);
	writer.Load();
	writer.Save(MakeChanged(m_home), AllChanges);
	reader.Initialize(m_configHome, m_home);

	EXPECT_EQ(reader.Load(), MakeChanged(m_home));
}

//////////////////////////////////////////////////////////////////////////
TEST_F(CSettingsStoreTest, TokenChangeKeepsTheStoredChoices)
{
	Config::CSettingsStore writer{};
	Config::CSettingsStore reader{};

	writer.Initialize(m_configHome, m_home);
	writer.Load();
	writer.Save(MakeChanged(m_home), AllChanges);

	Recorder::SSettings fallen{ MakeChanged(m_home) };

	fallen.audio[System].device = "the first sink";
	fallen.screenToken = "new-token";
	writer.Save(fallen, { .screenToken = true });
	reader.Initialize(m_configHome, m_home);

	Recorder::SSettings const loaded{ reader.Load() };

	EXPECT_EQ(loaded.audio[System].device, "sink");
	EXPECT_EQ(loaded.screenToken, "new-token");
}

//////////////////////////////////////////////////////////////////////////
TEST_F(CSettingsStoreTest, NothingChangedSavesNothing)
{
	Config::CSettingsStore store{};

	store.Initialize(m_configHome, m_home);
	store.Load();
	store.Save(MakeChanged(m_home), {});

	EXPECT_FALSE(std::filesystem::exists(GetSettingsPath()));
}

//////////////////////////////////////////////////////////////////////////
TEST_F(CSettingsStoreTest, UnchangedFileIsNotRewritten)
{
	Config::CSettingsStore store{};

	store.Initialize(m_configHome, m_home);
	store.Load();
	store.Save(MakeChanged(m_home), AllChanges);

	ino_t const before{ GetInode(GetSettingsPath()) };

	store.Save(MakeChanged(m_home), AllChanges);

	EXPECT_EQ(GetInode(GetSettingsPath()), before);
}

//////////////////////////////////////////////////////////////////////////
TEST_F(CSettingsStoreTest, UnknownKeysSurviveASave)
{
	Config::CSettingsStore store{};

	Tests::WriteText(GetSettingsPath(), R"({ "future": 1, "output": { "later": true } })");
	store.Initialize(m_configHome, m_home);
	store.Load();
	store.Save(MakeChanged(m_home), AllChanges);

	std::string const text{ Tests::ReadText(GetSettingsPath()) };

	EXPECT_NE(text.find("\"future\": 1"), std::string::npos) << text;
	EXPECT_NE(text.find("\"later\": true"), std::string::npos) << text;
}

//////////////////////////////////////////////////////////////////////////
TEST_F(CSettingsStoreTest, InvalidValueKeepsItsDefault)
{
	Config::CSettingsStore store{};

	Tests::WriteText(GetSettingsPath(), R"({ "output": { "codec": "vp9", "directory": "/srv/clips" } })");
	store.Initialize(m_configHome, m_home);

	Tge::Testing::CExpectedLog const expected{ "Config", 1, 0 };
	Recorder::SSettings const        loaded{ store.Load() };

	EXPECT_EQ(loaded.codec, Recorder::SSettings{}.codec);
	EXPECT_EQ(loaded.directory, "/srv/clips");
}

//////////////////////////////////////////////////////////////////////////
TEST_F(CSettingsStoreTest, InvalidValueIsBackedUp)
{
	Config::CSettingsStore store{};
	std::string const      original{ R"({ "capture": { "frameRate": 45 } })" };

	Tests::WriteText(GetSettingsPath(), original);
	store.Initialize(m_configHome, m_home);

	Tge::Testing::CExpectedLog const expected{ "Config", 1, 0 };

	store.Load();

	EXPECT_EQ(Tests::ReadText(GetSettingsPath().string() + ".bad"), original);
}

//////////////////////////////////////////////////////////////////////////
TEST_F(CSettingsStoreTest, TextThatIsNotJsonIsMovedAside)
{
	Config::CSettingsStore store{};

	Tests::WriteText(GetSettingsPath(), "{ nope");
	store.Initialize(m_configHome, m_home);

	Tge::Testing::CExpectedLog const expected{ "Config", 1, 0 };

	EXPECT_EQ(store.Load(), MakeDefaults(m_home));
	EXPECT_FALSE(std::filesystem::exists(GetSettingsPath()));
	EXPECT_EQ(Tests::ReadText(GetSettingsPath().string() + ".bad"), "{ nope");
}

//////////////////////////////////////////////////////////////////////////
TEST_F(CSettingsStoreTest, NewerFormatIsNeverSavedOver)
{
	Config::CSettingsStore store{};
	std::string const      newer{ R"({ "version": 2 })" };

	Tests::WriteText(GetSettingsPath(), newer);
	store.Initialize(m_configHome, m_home);

	{
		Tge::Testing::CExpectedLog const expected{ "Config", 1, 0 };

		store.Load();
	}

	store.Save(MakeChanged(m_home), AllChanges);

	EXPECT_EQ(Tests::ReadText(GetSettingsPath()), newer);
}
