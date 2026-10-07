#include "config/config_test.hpp"
#include "config/settings_store.hpp"
#include "encode/format.hpp"
#include "encode/quality.hpp"
#include "recorder/audio_source.hpp"
#include "recorder/settings.hpp"
#include "recorder/source.hpp"

#include <gtest/gtest.h>
#include <tge/testing/expected_log.hpp>

#include <cstddef>
#include <filesystem>
#include <string_view>

using namespace Klip;

namespace
{
using CKlipConfImportTest = Tests::CConfigTest;

constexpr size_t System{ static_cast<size_t>(Recorder::EAudioSource::System) };
constexpr size_t Microphone{ static_cast<size_t>(Recorder::EAudioSource::Microphone) };

// Written by QSettings 6.10 from Klip's keys, with values chosen to need its quoting and escapes.
constexpr std::string_view QtWritten{ R"conf([audio]
microphoneDevice=@@mic\ttab
microphoneEnabled=false
microphoneGain=8
quality=smaller
systemDevice=alsa_output.pci-0000_0f_00.6.analog-stereo
systemEnabled=true
systemGain=-12

[capture]
frameRate=60
rememberWindow=true
source=2

[output]
codec=hevc
container=matroska
directory="/home/user/Vidéos/a,b \"q\" back\\slash;x"
quality=best

[portal]
restoreToken\screen=9597bdca-36bd-4ea7-b7cf-2f81b3073d8c
restoreToken\window=w-token
)conf" };

//////////////////////////////////////////////////////////////////////////
Recorder::SSettings MakeQtWrittenSettings()
{
	Recorder::SSettings settings{};

	settings.directory = "/home/user/Vidéos/a,b \"q\" back\\slash;x";
	settings.container = Encode::EContainer::Matroska;
	settings.codec = Encode::ECodec::Hevc;
	settings.quality = Encode::EQuality::Best;
	settings.source = Recorder::ESource::Region;
	settings.maxFrameRate = 60;
	settings.rememberWindow = true;
	settings.audio[System] = { .enabled = true, .device = "alsa_output.pci-0000_0f_00.6.analog-stereo",
		                       .gainDecibels = -12 };
	settings.audio[Microphone] = { .enabled = false, .device = "@mic\ttab", .gainDecibels = 8 };
	settings.audioQuality = Encode::EQuality::Smaller;
	settings.screenToken = "9597bdca-36bd-4ea7-b7cf-2f81b3073d8c";
	settings.windowToken = "w-token";

	return settings;
}
} // namespace

//////////////////////////////////////////////////////////////////////////
TEST_F(CKlipConfImportTest, EverySettingQtWroteIsImported)
{
	Tge::Testing::CExpectedLog const expected{ "Config", 0, 0 };
	Config::CSettingsStore           store{};

	Tests::WriteText(GetLegacyPath(), QtWritten);
	store.Initialize(m_configHome, m_home);

	EXPECT_EQ(store.Load(), MakeQtWrittenSettings());
}

//////////////////////////////////////////////////////////////////////////
TEST_F(CKlipConfImportTest, ImportIsSavedAtOnce)
{
	Config::CSettingsStore importer{};
	Config::CSettingsStore reader{};

	Tests::WriteText(GetLegacyPath(), QtWritten);
	importer.Initialize(m_configHome, m_home);
	importer.Load();
	std::filesystem::remove(GetLegacyPath());
	reader.Initialize(m_configHome, m_home);

	EXPECT_EQ(reader.Load(), MakeQtWrittenSettings());
}

//////////////////////////////////////////////////////////////////////////
TEST_F(CKlipConfImportTest, KlipConfIsLeftInPlace)
{
	Config::CSettingsStore store{};

	Tests::WriteText(GetLegacyPath(), QtWritten);
	store.Initialize(m_configHome, m_home);
	store.Load();

	EXPECT_EQ(Tests::ReadText(GetLegacyPath()), QtWritten);
}

//////////////////////////////////////////////////////////////////////////
TEST_F(CKlipConfImportTest, ImportHappensOnlyOnce)
{
	Config::CSettingsStore importer{};
	Config::CSettingsStore reader{};

	Tests::WriteText(GetLegacyPath(), QtWritten);
	importer.Initialize(m_configHome, m_home);
	importer.Load();
	Tests::WriteText(GetLegacyPath(), "[output]\ncodec=av1\n");
	reader.Initialize(m_configHome, m_home);

	EXPECT_EQ(reader.Load().codec, Encode::ECodec::Hevc);
}

//////////////////////////////////////////////////////////////////////////
TEST_F(CKlipConfImportTest, ValueKlipNeverWroteKeepsItsDefault)
{
	Config::CSettingsStore store{};

	Tests::WriteText(GetLegacyPath(), "[capture]\nsource=7\nframeRate=30\n");
	store.Initialize(m_configHome, m_home);

	Tge::Testing::CExpectedLog const expected{ "Config", 1, 0 };
	Recorder::SSettings const        loaded{ store.Load() };

	EXPECT_EQ(loaded.source, Recorder::SSettings{}.source);
	EXPECT_EQ(loaded.maxFrameRate, 30u);
}
