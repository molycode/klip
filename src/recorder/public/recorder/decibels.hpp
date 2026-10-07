#pragma once

#include <cmath>

namespace Klip::Recorder
{
inline constexpr float FloorDecibels{ -60.0f };

//////////////////////////////////////////////////////////////////////////
inline float ToDecibels(float level)
{
	return level > 0.0f ? 20.0f * std::log10(level) : FloorDecibels;
}

//////////////////////////////////////////////////////////////////////////
inline float FromDecibels(float decibels)
{
	return decibels <= FloorDecibels ? 0.0f : std::pow(10.0f, decibels / 20.0f);
}
} // namespace Klip::Recorder
