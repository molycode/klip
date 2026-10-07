#include "recorder/decibels.hpp"
#include "recorder/level_ballistics.hpp"

#include <gtest/gtest.h>

#include <array>
#include <chrono>

using namespace Klip;
using namespace std::chrono_literals;

namespace
{
using TimePoint = Recorder::CLevelBallistics::TimePoint;

TimePoint const gStart{};

//////////////////////////////////////////////////////////////////////////
void Feed(Recorder::CLevelBallistics& meter, float peak, std::chrono::milliseconds at)
{
	std::array<float, 1> const peaks{ peak };

	meter.Feed(peaks, gStart + at);
}
} // namespace

//////////////////////////////////////////////////////////////////////////
TEST(LevelBallistics, LevelJumpsToAPeak)
{
	Recorder::CLevelBallistics meter;

	Feed(meter, 0.5f, 0ms);

	EXPECT_FLOAT_EQ(meter.GetLevel(0), 0.5f);
}

//////////////////////////////////////////////////////////////////////////
TEST(LevelBallistics, LevelFallsSixtyDecibelsASecond)
{
	Recorder::CLevelBallistics meter;

	Feed(meter, 0.5f, 0ms);
	Feed(meter, 0.0f, 500ms);

	EXPECT_NEAR(meter.GetLevel(0), Recorder::FromDecibels(Recorder::ToDecibels(0.5f) - 30.0f), 1e-6f);
}

//////////////////////////////////////////////////////////////////////////
TEST(LevelBallistics, HoldStaysForASecondAndAHalf)
{
	Recorder::CLevelBallistics meter;

	Feed(meter, 0.5f, 0ms);
	Feed(meter, 0.0f, 1400ms);

	EXPECT_FLOAT_EQ(meter.GetHold(0), 0.5f);
}

//////////////////////////////////////////////////////////////////////////
TEST(LevelBallistics, HoldThenFallsTwentyDecibelsASecond)
{
	Recorder::CLevelBallistics meter;

	Feed(meter, 0.5f, 0ms);
	Feed(meter, 0.0f, 2500ms);

	EXPECT_NEAR(meter.GetHold(0), Recorder::FromDecibels(Recorder::ToDecibels(0.5f) - 20.0f), 1e-6f);
}

//////////////////////////////////////////////////////////////////////////
TEST(LevelBallistics, ClipStaysLatchedForThreeSeconds)
{
	Recorder::CLevelBallistics meter;

	Feed(meter, 1.0f, 0ms);
	Feed(meter, 0.1f, 2900ms);

	EXPECT_TRUE(meter.IsClipped(0));
}

//////////////////////////////////////////////////////////////////////////
TEST(LevelBallistics, ClipClearsAfterThreeSeconds)
{
	Recorder::CLevelBallistics meter;

	Feed(meter, 1.0f, 0ms);
	Feed(meter, 0.1f, 3100ms);

	EXPECT_FALSE(meter.IsClipped(0));
}

//////////////////////////////////////////////////////////////////////////
TEST(LevelBallistics, ResetForgetsLevelHoldAndClip)
{
	Recorder::CLevelBallistics meter;

	Feed(meter, 1.0f, 0ms);
	meter.Reset();

	EXPECT_EQ(meter.GetLevel(0), 0.0f);
	EXPECT_EQ(meter.GetHold(0), 0.0f);
	EXPECT_FALSE(meter.IsClipped(0));
}
