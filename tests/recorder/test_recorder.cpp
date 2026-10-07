#include "bus/connection.hpp"
#include "capture/fake_portal.hpp"
#include "capture/pipewire.hpp"
#include "recorder/recorder.hpp"

#include <gtest/gtest.h>
#include <tge/testing/expected_log.hpp>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <format>
#include <fstream>
#include <semaphore>
#include <string>
#include <unistd.h>

using namespace Klip;

namespace
{
// Only a hang ever waits this long; every answer arrives in milliseconds.
constexpr std::chrono::seconds Patience{ 10 };

Tests::CFakePortal gFakePortal;

std::atomic<uint32_t> gNumFolders{ 0 };

class CRecorderTest : public testing::Test
{
protected:

	static void SetUpTestSuite()
	{
		Capture::InitializePipeWire();

		ASSERT_TRUE(Bus::gConnection.Initialize("klip-bus"));
		ASSERT_TRUE(gFakePortal.Initialize()) << "Cannot own org.freedesktop.portal.Desktop on the test bus";
	}

	static void TearDownTestSuite()
	{
		gFakePortal.Terminate();
		Bus::gConnection.Terminate();
		Capture::TerminatePipeWire();
	}

	void SetUp() override
	{
		gFakePortal.Configure(Tests::SFakeScript{});

		m_folder = std::filesystem::temp_directory_path() /
		           std::format("klip-tests-{}-{}", getpid(), gNumFolders.fetch_add(1, std::memory_order_relaxed));
	}

	void TearDown() override
	{
		m_recorder.Terminate();

		std::error_code error;
		std::filesystem::remove_all(m_folder, error);
	}

	Recorder::SSettings MakeSettings() const
	{
		Recorder::SSettings settings;
		settings.directory = m_folder.string();

		return settings;
	}

	bool Initialize(Recorder::SSettings const& settings)
	{
		Encode::SCapabilities const card{ .devicePath = "/dev/dri/renderD128", .encodes = { true, true, true } };

		return m_recorder.Initialize(settings, Recorder::SScreen{ 2880, 1620, 60 }, card, Desktop::STrayIcons{}, {},
		                             [this]() { m_wakes.release(); });
	}

	// Through every step the window takes, then the portal's answer.
	void StartAndAnswer()
	{
		ASSERT_TRUE(m_recorder.PrepareRecording());

		m_recorder.BeginRecording(Encode::SRegion{});
		m_recorder.RequestCapture();

		ASSERT_TRUE(m_wakes.try_acquire_for(Patience)) << "The portal never answered";

		m_recorder.Update();
	}

	Recorder::CRecorder       m_recorder;
	std::counting_semaphore<> m_wakes{ 0 };
	std::filesystem::path     m_folder;
};
} // namespace

//////////////////////////////////////////////////////////////////////////
TEST_F(CRecorderTest, WebMOffersOnlyAv1)
{
	ASSERT_TRUE(Initialize(MakeSettings()));

	m_recorder.SetContainer(Encode::EContainer::WebM);

	ASSERT_EQ(m_recorder.GetCodecs().size(), 1u);
	EXPECT_EQ(m_recorder.GetCodecs().front(), Encode::ECodec::Av1);
}

//////////////////////////////////////////////////////////////////////////
TEST_F(CRecorderTest, ChangingToWebMMovesTheCodecToAv1)
{
	ASSERT_TRUE(Initialize(MakeSettings()));

	m_recorder.SetContainer(Encode::EContainer::WebM);

	EXPECT_EQ(m_recorder.GetSettings().codec, Encode::ECodec::Av1);
}

//////////////////////////////////////////////////////////////////////////
TEST_F(CRecorderTest, LoadingNormalisesWithoutReportingAChange)
{
	Recorder::SSettings settings{ MakeSettings() };
	settings.container = Encode::EContainer::WebM;
	settings.codec = Encode::ECodec::H264;
	settings.maxFrameRate = 45;

	ASSERT_TRUE(Initialize(settings));

	Recorder::SSettingsChanges const changes{ m_recorder.TakeSettingsChanges() };

	EXPECT_EQ(m_recorder.GetSettings().codec, Encode::ECodec::Av1);
	EXPECT_EQ(m_recorder.GetSettings().maxFrameRate, Recorder::NoFrameRateCap);
	EXPECT_FALSE(changes.choices);
}

//////////////////////////////////////////////////////////////////////////
TEST_F(CRecorderTest, ShowingTheWindowReportsNoChange)
{
	ASSERT_TRUE(Initialize(MakeSettings()));

	m_recorder.RefreshAudioDevices();
	m_recorder.SetVisible(true);

	EXPECT_FALSE(m_recorder.TakeSettingsChanges().choices);
}

//////////////////////////////////////////////////////////////////////////
TEST_F(CRecorderTest, AChoiceIsReportedOnce)
{
	ASSERT_TRUE(Initialize(MakeSettings()));

	m_recorder.SetQuality(Encode::EQuality::Best);

	EXPECT_TRUE(m_recorder.TakeSettingsChanges().choices);
	EXPECT_FALSE(m_recorder.TakeSettingsChanges().choices);
}

//////////////////////////////////////////////////////////////////////////
TEST_F(CRecorderTest, NothingIsScheduledWhileIdle)
{
	ASSERT_TRUE(Initialize(MakeSettings()));

	m_recorder.SetVisible(true);

	EXPECT_FALSE(m_recorder.GetNextDeadline().has_value());
}

//////////////////////////////////////////////////////////////////////////
TEST_F(CRecorderTest, BeginningWaitsForPermission)
{
	ASSERT_TRUE(Initialize(MakeSettings()));
	ASSERT_TRUE(m_recorder.PrepareRecording());

	m_recorder.BeginRecording(Encode::SRegion{});

	EXPECT_EQ(m_recorder.GetState(), Recorder::EState::Starting);
	EXPECT_EQ(m_recorder.GetStatus(), "Waiting for permission…");
}

//////////////////////////////////////////////////////////////////////////
TEST_F(CRecorderTest, DismissedPickerEndsIdle)
{
	Tests::SFakeScript script;
	script.startResponse = 1;
	gFakePortal.Configure(script);

	ASSERT_TRUE(Initialize(MakeSettings()));

	StartAndAnswer();

	EXPECT_EQ(m_recorder.GetState(), Recorder::EState::Idle);
	EXPECT_EQ(m_recorder.GetStatus(), "Recording was not permitted");
}

//////////////////////////////////////////////////////////////////////////
TEST_F(CRecorderTest, DismissedPickerShowsTheWindowAgain)
{
	Tests::SFakeScript script;
	script.startResponse = 1;
	gFakePortal.Configure(script);

	ASSERT_TRUE(Initialize(MakeSettings()));

	StartAndAnswer();

	EXPECT_EQ(m_recorder.TakeReveal(), Recorder::EReveal::Show);
}

//////////////////////////////////////////////////////////////////////////
TEST_F(CRecorderTest, ScreenStartSendsTheKeptToken)
{
	Tests::SFakeScript script;
	script.startResponse = 1;
	gFakePortal.Configure(script);

	Recorder::SSettings settings{ MakeSettings() };
	settings.screenToken = "kept";

	ASSERT_TRUE(Initialize(settings));

	StartAndAnswer();

	EXPECT_EQ(gFakePortal.GetRecord().restoreToken, "kept");
	EXPECT_EQ(m_recorder.GetSettings().screenToken, "kept");
	EXPECT_FALSE(m_recorder.TakeSettingsChanges().screenToken);
}

//////////////////////////////////////////////////////////////////////////
TEST_F(CRecorderTest, WindowStartWithoutRememberDropsTheWindowToken)
{
	Tests::SFakeScript script;
	script.startResponse = 1;
	gFakePortal.Configure(script);

	Recorder::SSettings settings{ MakeSettings() };
	settings.source = Recorder::ESource::Window;
	settings.windowToken = "old";

	ASSERT_TRUE(Initialize(settings));

	StartAndAnswer();

	EXPECT_EQ(gFakePortal.GetRecord().restoreToken, "");
	EXPECT_EQ(m_recorder.GetSettings().windowToken, "");
	EXPECT_TRUE(m_recorder.TakeSettingsChanges().windowToken);
}

//////////////////////////////////////////////////////////////////////////
TEST_F(CRecorderTest, StartThatFailsAfterTheGrantStillKeepsTheNewToken)
{
	Tests::SFakeScript script;
	script.streams = { { .nodeId = 42, .width = 0, .height = 0, .hasSize = false } };
	gFakePortal.Configure(script);

	ASSERT_TRUE(Initialize(MakeSettings()));

	StartAndAnswer();

	EXPECT_EQ(m_recorder.GetState(), Recorder::EState::Idle);
	EXPECT_EQ(m_recorder.GetSettings().screenToken, "granted");
	EXPECT_TRUE(m_recorder.TakeSettingsChanges().screenToken);
}

//////////////////////////////////////////////////////////////////////////
TEST_F(CRecorderTest, FolderUnderAFileCannotBePrepared)
{
	ASSERT_TRUE(std::filesystem::create_directories(m_folder));
	std::ofstream{ m_folder / "file" } << "in the way";

	Recorder::SSettings settings{ MakeSettings() };
	settings.directory = (m_folder / "file" / "recordings").string();

	ASSERT_TRUE(Initialize(settings));

	Tge::Testing::CExpectedLog const expected{ "Recorder", 0, 1 };

	EXPECT_FALSE(m_recorder.PrepareRecording());
	EXPECT_EQ(m_recorder.GetStatus(), "Cannot write to that folder");
}

//////////////////////////////////////////////////////////////////////////
TEST_F(CRecorderTest, SecondStartWhileTheFirstAwaitsThePortalIsRefused)
{
	Tests::SFakeScript script;
	script.holdStart = true;
	gFakePortal.Configure(script);

	ASSERT_TRUE(Initialize(MakeSettings()));
	ASSERT_TRUE(m_recorder.PrepareRecording());

	m_recorder.BeginRecording(Encode::SRegion{});
	m_recorder.RequestCapture();

	EXPECT_FALSE(m_recorder.PrepareRecording());
	EXPECT_EQ(m_recorder.GetState(), Recorder::EState::Starting);
	EXPECT_EQ(m_recorder.GetStatus(), "Waiting for permission…");
	EXPECT_EQ(m_recorder.TakeReveal(), Recorder::EReveal::None);
}
