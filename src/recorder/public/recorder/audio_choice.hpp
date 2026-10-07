#pragma once

#include <string>

namespace Klip::Recorder
{
inline constexpr int MinimumGainDecibels{ -30 };
inline constexpr int MaximumGainDecibels{ 20 };

struct SAudioChoice final
{
	bool        enabled{ false };
	std::string device;
	int         gainDecibels{ 0 };
};
} // namespace Klip::Recorder
