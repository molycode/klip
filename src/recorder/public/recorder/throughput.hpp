#pragma once

#include <string>

namespace Klip::Recorder
{
// Both empty until enough has been written for a rate to mean anything.
struct SThroughput final
{
	std::string perMinute;
	std::string perHour;
};
} // namespace Klip::Recorder
