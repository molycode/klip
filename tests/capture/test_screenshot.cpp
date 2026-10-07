#include "bus/connection.hpp"
#include "capture/fake_portal.hpp"
#include "capture/screenshot.hpp"

#include <gtest/gtest.h>
#include <tge/testing/expected_log.hpp>

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <semaphore>
#include <string>
#include <system_error>
#include <unistd.h>

using namespace Klip;

namespace
{
// Only a hang ever waits this long; every answer arrives in milliseconds.
constexpr std::chrono::seconds Patience{ 10 };

Tests::CFakePortal gFakePortal;

class CScreenshotTest : public testing::Test
{
protected:

	static void SetUpTestSuite()
	{
		ASSERT_TRUE(Bus::gConnection.Initialize("klip-bus"));
		ASSERT_TRUE(gFakePortal.Initialize()) << "Cannot own org.freedesktop.portal.Desktop on the test bus";
	}

	static void TearDownTestSuite()
	{
		gFakePortal.Terminate();
		Bus::gConnection.Terminate();
	}

	void SetUp() override
	{
		gFakePortal.Configure(Tests::SFakeScript{});
	}

	void TearDown() override
	{
		m_screenshot.Cancel();
	}

	// The wake's semaphore outlives a test that gave up waiting.
	bool RequestAndWait()
	{
		auto pAnswered = std::make_shared<std::binary_semaphore>(0);

		return m_screenshot.Request([pAnswered]() { pAnswered->release(); }) &&
		       pAnswered->try_acquire_for(Patience);
	}

	Capture::CScreenshot m_screenshot;
};

//////////////////////////////////////////////////////////////////////////
std::filesystem::path MakeShotFile()
{
	std::error_code error{};
	std::string     pattern{ (std::filesystem::temp_directory_path(error) / "klip-shot-XXXXXX").string() };
	int const       fd{ ::mkstemp(pattern.data()) };

	EXPECT_GE(fd, 0);
	::close(fd);

	return pattern;
}
} // namespace

//////////////////////////////////////////////////////////////////////////
TEST_F(CScreenshotTest, AnswerIsTheLocalPath)
{
	Tests::SFakeScript script;
	script.screenshotUri = "file:///tmp/klip%20shot.png";
	gFakePortal.Configure(script);

	ASSERT_TRUE(RequestAndWait());
	EXPECT_EQ(m_screenshot.TakeAnswer(), std::optional<std::string>{ "/tmp/klip shot.png" });
}

//////////////////////////////////////////////////////////////////////////
TEST_F(CScreenshotTest, AnswerIsTakenOnce)
{
	ASSERT_TRUE(RequestAndWait());

	m_screenshot.TakeAnswer();

	EXPECT_EQ(m_screenshot.TakeAnswer(), std::nullopt);
}

//////////////////////////////////////////////////////////////////////////
TEST_F(CScreenshotTest, AsksWithoutTheDialog)
{
	ASSERT_TRUE(RequestAndWait());
	EXPECT_FALSE(gFakePortal.GetRecord().screenshotInteractive);
}

//////////////////////////////////////////////////////////////////////////
TEST_F(CScreenshotTest, RefusalAnswersWithNoPath)
{
	Tests::SFakeScript script;
	script.screenshotResponse = 2;
	gFakePortal.Configure(script);

	Tge::Testing::CExpectedLog expected{ "Capture", 0, 1 };

	ASSERT_TRUE(RequestAndWait());
	EXPECT_EQ(m_screenshot.TakeAnswer(), std::optional<std::string>{ "" });
}

//////////////////////////////////////////////////////////////////////////
TEST_F(CScreenshotTest, MissingPortalCannotBeAsked)
{
	gFakePortal.Withdraw();

	{
		Tge::Testing::CExpectedLog expected{ "Capture", 0, 1 };

		EXPECT_FALSE(m_screenshot.Request([]() {}));
	}

	ASSERT_TRUE(gFakePortal.Restore());
}

//////////////////////////////////////////////////////////////////////////
TEST_F(CScreenshotTest, CancelDeletesAnUntakenAnswer)
{
	std::filesystem::path const shot{ MakeShotFile() };

	Tests::SFakeScript script;
	script.screenshotUri = "file://" + shot.string();
	gFakePortal.Configure(script);

	ASSERT_TRUE(RequestAndWait());

	m_screenshot.Cancel();

	EXPECT_FALSE(std::filesystem::exists(shot));
}
