#include "bus/connection.hpp"

#include <gtest/gtest.h>
#include <systemd/sd-bus.h>
#include <tge/testing/expected_log.hpp>

#include <atomic>
#include <cstdint>
#include <string>
#include <sys/socket.h>

namespace
{
constexpr uint32_t NumPosts{ 100 };

class CConnectionTest : public testing::Test
{
protected:

	void SetUp() override
	{
		ASSERT_TRUE(m_connection.Initialize("klip-test-bus"));
	}

	void TearDown() override
	{
		m_connection.Terminate();
	}

	Klip::Bus::CConnection m_connection;
};
} // namespace

//////////////////////////////////////////////////////////////////////////
TEST_F(CConnectionTest, RunReturnsAfterItsTask)
{
	bool ran{ false };

	m_connection.Run([&ran](sd_bus*) { ran = true; });

	EXPECT_TRUE(ran);
}

//////////////////////////////////////////////////////////////////////////
TEST_F(CConnectionTest, CallsMethodsOnTheBus)
{
	std::string id;
	int result{ 0 };

	m_connection.Run([&id, &result](sd_bus* pBus) {
		sd_bus_error error{ SD_BUS_ERROR_NULL };
		sd_bus_message* pReply{ nullptr };

		result = sd_bus_call_method(pBus, "org.freedesktop.DBus", "/org/freedesktop/DBus", "org.freedesktop.DBus",
		                            "GetId", &error, &pReply, "");

		char const* pId{ nullptr };

		if (result >= 0)
		{
			result = sd_bus_message_read(pReply, "s", &pId);
		}

		if (result >= 0)
		{
			id = pId;
		}

		sd_bus_message_unref(pReply);
		sd_bus_error_free(&error);
	});

	EXPECT_GE(result, 0);
	EXPECT_FALSE(id.empty());
}

//////////////////////////////////////////////////////////////////////////
TEST_F(CConnectionTest, ClosedBusLogsOneErrorAndKeepsRunning)
{
	Tge::Testing::CExpectedLog expected{ "Bus", 0, 1 };

	m_connection.Run([](sd_bus* pBus) { shutdown(sd_bus_get_fd(pBus), SHUT_RDWR); });

	bool ran{ false };

	m_connection.Run([&ran](sd_bus*) { ran = true; });

	EXPECT_TRUE(ran);
}

//////////////////////////////////////////////////////////////////////////
TEST(CConnection, TerminateRunsWhatWasPostedBeforeIt)
{
	Klip::Bus::CConnection connection;
	std::atomic<uint32_t> numRan{ 0 };

	ASSERT_TRUE(connection.Initialize("klip-test-bus"));

	for (uint32_t i{ 0 }; i < NumPosts; ++i)
	{
		connection.Post([&numRan](sd_bus*) { numRan.fetch_add(1, std::memory_order_relaxed); });
	}

	connection.Terminate();

	EXPECT_EQ(numRan.load(std::memory_order_relaxed), NumPosts);
}
