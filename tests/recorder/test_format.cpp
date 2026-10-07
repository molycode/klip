#include "recorder/format.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <cstdint>

using namespace Klip;
using namespace std::chrono_literals;

namespace
{
constexpr uint64_t Mebibyte{ uint64_t{ 1024 } * 1024 };
} // namespace

//////////////////////////////////////////////////////////////////////////
TEST(Format, DurationPadsEveryField)
{
	EXPECT_EQ(Recorder::FormatDuration(3723s), "01:02:03");
}

//////////////////////////////////////////////////////////////////////////
TEST(Format, DurationKeepsCountingPastNinetyNineHours)
{
	EXPECT_EQ(Recorder::FormatDuration(100h), "100:00:00");
}

//////////////////////////////////////////////////////////////////////////
TEST(Format, BytesReadInTenthsOfAMebibyte)
{
	EXPECT_EQ(Recorder::FormatBytes(Mebibyte * 3 / 2), "1.5 MiB");
}

//////////////////////////////////////////////////////////////////////////
TEST(Format, ThroughputWaitsForTheRateToSettle)
{
	Recorder::SThroughput const rate{ Recorder::FormatThroughput(Mebibyte * 10, 2s) };

	EXPECT_TRUE(rate.perMinute.empty());
	EXPECT_TRUE(rate.perHour.empty());
}

//////////////////////////////////////////////////////////////////////////
TEST(Format, ThroughputReadsPerMinute)
{
	EXPECT_EQ(Recorder::FormatThroughput(Mebibyte * 60, 60s).perMinute, "60.0 MiB/min");
}

//////////////////////////////////////////////////////////////////////////
TEST(Format, ThroughputPerHourStaysInMebibytesBelowAGibibyte)
{
	EXPECT_EQ(Recorder::FormatThroughput(Mebibyte * 10, 60s).perHour, "600 MiB/h");
}

//////////////////////////////////////////////////////////////////////////
TEST(Format, ThroughputPerHourTurnsToGibibytesFromOne)
{
	EXPECT_EQ(Recorder::FormatThroughput(Mebibyte * 60, 60s).perHour, "3.5 GiB/h");
}
