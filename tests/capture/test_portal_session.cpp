#include "bus/connection.hpp"
#include "capture/fake_portal.hpp"
#include "capture/portal_session.hpp"

#include <gtest/gtest.h>
#include <systemd/sd-bus.h>
#include <tge/testing/expected_log.hpp>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <semaphore>
#include <string>
#include <unistd.h>

using namespace Klip;
using namespace std::chrono_literals;

namespace
{
// Only a hang ever waits this long; every answer arrives in milliseconds.
constexpr std::chrono::seconds Patience{ 10 };

Tests::CFakePortal gFakePortal;

struct SWaiter final
{
	std::binary_semaphore done{ 0 };
	Capture::SPortalGrant grant;
};

class CPortalSessionTest : public testing::Test
{
protected:

	static void SetUpTestSuite()
	{
		ASSERT_TRUE(Bus::gConnection.Initialize("klip-bus"));

		// Without the name every test fails, so the suite stops here instead.
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
		ASSERT_TRUE(InitializePortal());
	}

	void TearDown() override
	{
		m_portal.Terminate();
	}

	bool InitializePortal()
	{
		return m_portal.Initialize([this]() {
			m_numClosed.fetch_add(1, std::memory_order_relaxed);
			m_closed.release();
		});
	}

	// Shared with the callback, which a test that gave up waiting must not leave pointing at its stack.
	std::shared_ptr<SWaiter> Start(Capture::ESourceType source, bool rememberWindow, std::string restoreToken)
	{
		auto pWaiter = std::make_shared<SWaiter>();

		m_portal.Start(source, rememberWindow, std::move(restoreToken), [pWaiter](Capture::SPortalGrant const& grant) {
			pWaiter->grant = grant;
			pWaiter->done.release();
		});

		return pWaiter;
	}

	Capture::SPortalGrant StartAndWait(Capture::ESourceType source = Capture::ESourceType::Screen,
	                                   bool rememberWindow = false, std::string restoreToken = {})
	{
		std::shared_ptr<SWaiter> const pWaiter{ Start(source, rememberWindow, std::move(restoreToken)) };
		Capture::SPortalGrant grant;

		if (pWaiter->done.try_acquire_for(Patience))
		{
			grant = pWaiter->grant;
		}
		else
		{
			ADD_FAILURE() << "The portal session never answered";
		}

		return grant;
	}

	// Anything the fake sent before answering this has been dispatched on Klip's side once it returns.
	static void RoundTrip()
	{
		Bus::gConnection.Run([](sd_bus* pBus) {
			uint32_t version{ 0 };

			sd_bus_get_property_trivial(pBus, Bus::PortalService, Bus::PortalPath, "org.freedesktop.portal.ScreenCast",
			                            "version", nullptr, 'u', &version);
		});
	}

	static void Close(Capture::SPortalGrant const& grant)
	{
		if (grant.pipeWireFd >= 0)
		{
			close(grant.pipeWireFd);
		}
	}

	Capture::CPortalSession m_portal;
	std::binary_semaphore   m_closed{ 0 };
	std::atomic<uint32_t>   m_numClosed{ 0 };
};
} // namespace

//////////////////////////////////////////////////////////////////////////
TEST_F(CPortalSessionTest, GrantsTheStreamItWasGiven)
{
	Capture::SPortalGrant const grant{ StartAndWait() };

	EXPECT_EQ(grant.result, Capture::EPortalResult::Success);
	EXPECT_EQ(grant.stream.nodeId, 42u);
	EXPECT_EQ(grant.stream.width, 2880u);
	EXPECT_EQ(grant.stream.height, 1620u);

	Close(grant);
}

//////////////////////////////////////////////////////////////////////////
TEST_F(CPortalSessionTest, HandsOverALiveRemote)
{
	Capture::SPortalGrant const grant{ StartAndWait() };
	int const peer{ gFakePortal.GetRecord().remotePeer };
	char const sent{ 'k' };
	char received{ 0 };

	ASSERT_GE(grant.pipeWireFd, 0);
	ASSERT_GE(peer, 0);
	EXPECT_EQ(write(grant.pipeWireFd, &sent, 1), 1);
	EXPECT_EQ(read(peer, &received, 1), 1);
	EXPECT_EQ(received, sent);

	Close(grant);
}

//////////////////////////////////////////////////////////////////////////
TEST_F(CPortalSessionTest, ReturnsTheGrantedRestoreToken)
{
	Capture::SPortalGrant const grant{ StartAndWait() };

	EXPECT_EQ(grant.restoreToken, "granted");

	Close(grant);
}

//////////////////////////////////////////////////////////////////////////
TEST_F(CPortalSessionTest, KeepsTheTokenWhenTheStreamIsUnusable)
{
	Tests::SFakeScript script;
	script.streams = { { .nodeId = 42, .width = 0, .height = 0, .hasSize = false } };
	gFakePortal.Configure(script);

	Tge::Testing::CExpectedLog expected{ "Capture", 0, 1 };
	Capture::SPortalGrant const grant{ StartAndWait() };

	EXPECT_EQ(grant.result, Capture::EPortalResult::Failed);
	EXPECT_EQ(grant.restoreToken, "granted");
}

//////////////////////////////////////////////////////////////////////////
TEST_F(CPortalSessionTest, DismissedPickerIsCancelledAndClosesTheSession)
{
	Tests::SFakeScript script;
	script.startResponse = 1;
	gFakePortal.Configure(script);

	Tge::Testing::CExpectedLog expected{ "Capture", 1, 0 };
	Capture::SPortalGrant const grant{ StartAndWait() };

	EXPECT_EQ(grant.result, Capture::EPortalResult::Cancelled);
	EXPECT_EQ(gFakePortal.GetRecord().numCloses, 1u);
}

//////////////////////////////////////////////////////////////////////////
TEST_F(CPortalSessionTest, RefusedSourcesFailAndCloseTheSession)
{
	Tests::SFakeScript script;
	script.selectSourcesResponse = 2;
	gFakePortal.Configure(script);

	Tge::Testing::CExpectedLog expected{ "Capture", 0, 1 };
	Capture::SPortalGrant const grant{ StartAndWait() };

	EXPECT_EQ(grant.result, Capture::EPortalResult::Failed);
	EXPECT_EQ(gFakePortal.GetRecord().numCloses, 1u);
}

//////////////////////////////////////////////////////////////////////////
TEST_F(CPortalSessionTest, RecordsTheFirstOfSeveralStreams)
{
	Tests::SFakeScript script;
	script.streams = { { .nodeId = 7, .width = 800, .height = 600, .hasSize = true },
	                   { .nodeId = 8, .width = 1024, .height = 768, .hasSize = true } };
	gFakePortal.Configure(script);

	Tge::Testing::CExpectedLog expected{ "Capture", 1, 0 };
	Capture::SPortalGrant const grant{ StartAndWait() };

	EXPECT_EQ(grant.result, Capture::EPortalResult::Success);
	EXPECT_EQ(grant.stream.nodeId, 7u);

	Close(grant);
}

//////////////////////////////////////////////////////////////////////////
TEST_F(CPortalSessionTest, MissingPortalFailsInitialize)
{
	m_portal.Terminate();
	gFakePortal.Withdraw();

	{
		Tge::Testing::CExpectedLog expected{ "Capture", 0, 1 };

		EXPECT_FALSE(InitializePortal());
	}

	ASSERT_TRUE(gFakePortal.Restore());
}

//////////////////////////////////////////////////////////////////////////
TEST_F(CPortalSessionTest, PortalWithoutPersistenceFailsInitialize)
{
	Tests::SFakeScript script;
	script.version = 3;
	gFakePortal.Configure(script);
	m_portal.Terminate();

	Tge::Testing::CExpectedLog expected{ "Capture", 0, 1 };

	EXPECT_FALSE(InitializePortal());
}

//////////////////////////////////////////////////////////////////////////
TEST_F(CPortalSessionTest, CompositorEndingTheCastIsReportedOnce)
{
	Capture::SPortalGrant const grant{ StartAndWait() };

	Tge::Testing::CExpectedLog expected{ "Capture", 1, 0 };

	gFakePortal.CloseFromCompositor();
	gFakePortal.CloseFromCompositor();

	EXPECT_TRUE(m_closed.try_acquire_for(Patience));
	RoundTrip();
	EXPECT_EQ(m_numClosed.load(std::memory_order_relaxed), 1u);

	Close(grant);
}

//////////////////////////////////////////////////////////////////////////
TEST_F(CPortalSessionTest, ClosingItsOwnSessionReportsNothing)
{
	Capture::SPortalGrant const grant{ StartAndWait() };

	m_portal.Close();
	RoundTrip();

	EXPECT_EQ(gFakePortal.GetRecord().numCloses, 1u);
	EXPECT_EQ(m_numClosed.load(std::memory_order_relaxed), 0u);

	Close(grant);
}

//////////////////////////////////////////////////////////////////////////
TEST_F(CPortalSessionTest, SecondStartWhileBusyIsRefused)
{
	Tests::SFakeScript script;
	script.holdStart = true;
	gFakePortal.Configure(script);

	Tge::Testing::CExpectedLog expected{ "Capture", 0, 1 };
	std::shared_ptr<SWaiter> const pFirst{ Start(Capture::ESourceType::Screen, false, {}) };
	Capture::SPortalGrant const second{ StartAndWait() };

	gFakePortal.ReleaseStart();

	ASSERT_TRUE(pFirst->done.try_acquire_for(Patience));
	EXPECT_EQ(second.result, Capture::EPortalResult::Failed);
	EXPECT_EQ(pFirst->grant.result, Capture::EPortalResult::Success);

	Close(pFirst->grant);
}

//////////////////////////////////////////////////////////////////////////
TEST_F(CPortalSessionTest, ForgottenWindowIsNotPersisted)
{
	Capture::SPortalGrant const grant{ StartAndWait(Capture::ESourceType::Window, false) };
	Tests::SFakeRecord const record{ gFakePortal.GetRecord() };

	EXPECT_EQ(record.types, 2u);
	EXPECT_EQ(record.persistMode, 0u);

	Close(grant);
}

//////////////////////////////////////////////////////////////////////////
TEST_F(CPortalSessionTest, RememberedWindowSendsItsToken)
{
	Capture::SPortalGrant const grant{ StartAndWait(Capture::ESourceType::Window, true, "kept") };
	Tests::SFakeRecord const record{ gFakePortal.GetRecord() };

	EXPECT_EQ(record.persistMode, 2u);
	EXPECT_EQ(record.restoreToken, "kept");

	Close(grant);
}

//////////////////////////////////////////////////////////////////////////
TEST_F(CPortalSessionTest, ScreenIsAlwaysPersisted)
{
	Capture::SPortalGrant const grant{ StartAndWait(Capture::ESourceType::Screen, false) };
	Tests::SFakeRecord const record{ gFakePortal.GetRecord() };

	EXPECT_EQ(record.types, 1u);
	EXPECT_EQ(record.persistMode, 2u);

	Close(grant);
}
