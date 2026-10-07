#include "bus/connection.hpp"
#include "bus/portal.hpp"
#include "desktop/single_instance.hpp"

#include <gtest/gtest.h>
#include <systemd/sd-bus.h>
#include <tge/testing/expected_log.hpp>
#include <tge/threading/mpsc_queue.hpp>

#include <string>
#include <string_view>
#include <vector>

using namespace Klip;

namespace
{
constexpr char const* Name{ "io.github.molycode.Klip" };
constexpr char const* ObjectPath{ "/io/github/molycode/Klip" };
constexpr char const* ApplicationInterface{ "org.freedesktop.Application" };

// The other Klip, on a connection of its own. What it records is touched only on its thread.
Bus::CConnection gOther;
sd_bus_slot*      gOtherSlot{ nullptr };
std::vector<std::string> gTokensReceived;

int RecordActivate(sd_bus_message* pCall, void*, sd_bus_error*)
{
	std::string token;

	Bus::ReadDict(pCall, [&token](std::string_view key, sd_bus_message* pEntry) {
		return key == "activation-token" && Bus::ReadString(pEntry, token);
	});

	gTokensReceived.push_back(token);

	return sd_bus_reply_method_return(pCall, "");
}

sd_bus_vtable const OtherVtable[]{
	SD_BUS_VTABLE_START(0),
	SD_BUS_METHOD("Activate", "a{sv}", "", RecordActivate, 0),
	SD_BUS_VTABLE_END
};

class CSingleInstanceTest : public testing::Test
{
protected:

	static void SetUpTestSuite()
	{
		ASSERT_TRUE(Bus::gConnection.Initialize("klip-bus"));
		ASSERT_TRUE(gOther.Initialize("other-klip"));
	}

	static void TearDownTestSuite()
	{
		gOther.Terminate();
		Bus::gConnection.Terminate();
	}

	void TearDown() override
	{
		m_instance.Terminate();

		gOther.Run([](sd_bus* pBus) {
			gOtherSlot = sd_bus_slot_unref(gOtherSlot);
			gTokensReceived.clear();
			sd_bus_release_name(pBus, Name);
		});
	}

	// The other Klip got there first.
	static void OtherOwnsTheName()
	{
		gOther.Run([](sd_bus* pBus) {
			sd_bus_add_object_vtable(pBus, &gOtherSlot, ObjectPath, ApplicationInterface, OtherVtable, nullptr);
			sd_bus_request_name(pBus, Name, 0);
		});
	}

	static std::vector<std::string> TokensReceived()
	{
		std::vector<std::string> tokens;

		gOther.Run([&tokens](sd_bus*) { tokens = gTokensReceived; });

		return tokens;
	}

	static bool NameIsOwned()
	{
		int owned{ 0 };

		gOther.Run([&owned](sd_bus* pBus) {
			sd_bus_message* pReply{ nullptr };

			if (sd_bus_call_method(pBus, "org.freedesktop.DBus", "/org/freedesktop/DBus", "org.freedesktop.DBus",
			                       "NameHasOwner", nullptr, &pReply, "s", Name) >= 0)
			{
				sd_bus_message_read(pReply, "b", &owned);
			}

			sd_bus_message_unref(pReply);
		});

		return owned != 0;
	}

	// The other Klip asking this one to show itself.
	static int Activate(std::vector<Bus::SOption> const& platformData)
	{
		int result{ 0 };

		gOther.Run([&result, &platformData](sd_bus* pBus) {
			sd_bus_message* pCall{ nullptr };

			result = sd_bus_message_new_method_call(pBus, &pCall, Name, ObjectPath, ApplicationInterface, "Activate");

			if (result >= 0)
			{
				result = Bus::AppendOptions(pCall, platformData);
			}

			if (result >= 0)
			{
				result = sd_bus_call(pBus, pCall, 0, nullptr, nullptr);
			}

			sd_bus_message_unref(pCall);
		});

		return result;
	}

	void Serve()
	{
		m_instance.Serve([this](Desktop::SRequest const& request) { m_requests.Enqueue(request); });
	}

	std::vector<Desktop::SRequest> TakeRequests()
	{
		std::vector<Desktop::SRequest> taken;
		Desktop::SRequest request;

		while (m_requests.Dequeue(request))
		{
			taken.push_back(request);
		}

		return taken;
	}

	Desktop::CSingleInstance                      m_instance;
	Tge::Threading::CMpscQueue<Desktop::SRequest> m_requests;
};
} // namespace

//////////////////////////////////////////////////////////////////////////
TEST_F(CSingleInstanceTest, ClaimsAFreeName)
{
	EXPECT_TRUE(m_instance.Claim());
	EXPECT_TRUE(NameIsOwned());
}

//////////////////////////////////////////////////////////////////////////
TEST_F(CSingleInstanceTest, TerminateReleasesTheName)
{
	ASSERT_TRUE(m_instance.Claim());

	m_instance.Terminate();

	EXPECT_FALSE(NameIsOwned());
}

//////////////////////////////////////////////////////////////////////////
TEST_F(CSingleInstanceTest, TakenNameIsRefused)
{
	OtherOwnsTheName();

	Tge::Testing::CExpectedLog expected{ "Desktop", 0, 0 };

	EXPECT_FALSE(m_instance.Claim());
}

//////////////////////////////////////////////////////////////////////////
TEST_F(CSingleInstanceTest, AskingTheOwnerPassesTheToken)
{
	OtherOwnsTheName();

	m_instance.AskOwnerToShow("token-7");

	EXPECT_EQ(TokensReceived(), std::vector<std::string>{ "token-7" });
}

//////////////////////////////////////////////////////////////////////////
TEST_F(CSingleInstanceTest, AskingTheOwnerWithoutATokenSendsNone)
{
	OtherOwnsTheName();

	m_instance.AskOwnerToShow("");

	EXPECT_EQ(TokensReceived(), std::vector<std::string>{ "" });
}

//////////////////////////////////////////////////////////////////////////
TEST_F(CSingleInstanceTest, AskingWhenNobodyAnswersIsAWarning)
{
	Tge::Testing::CExpectedLog expected{ "Desktop", 1, 0 };

	m_instance.AskOwnerToShow("token-7");
}

//////////////////////////////////////////////////////////////////////////
TEST_F(CSingleInstanceTest, ActivateAsksToShow)
{
	ASSERT_TRUE(m_instance.Claim());
	Serve();

	EXPECT_GE(Activate({}), 0);

	std::vector<Desktop::SRequest> const requests{ TakeRequests() };

	ASSERT_EQ(requests.size(), 1u);
	EXPECT_EQ(requests[0].kind, Desktop::ERequest::Show);
}

//////////////////////////////////////////////////////////////////////////
TEST_F(CSingleInstanceTest, ActivateHandsOverTheTokenFirst)
{
	ASSERT_TRUE(m_instance.Claim());
	Serve();

	EXPECT_GE(Activate({ { "activation-token", std::string{ "token-9" } } }), 0);

	std::vector<Desktop::SRequest> const requests{ TakeRequests() };

	ASSERT_EQ(requests.size(), 2u);
	EXPECT_EQ(requests[0].kind, Desktop::ERequest::ActivationToken);
	EXPECT_EQ(requests[0].token, "token-9");
	EXPECT_EQ(requests[1].kind, Desktop::ERequest::Show);
}
