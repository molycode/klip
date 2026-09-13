#pragma once

#include <string>

namespace Klip::Capture
{
struct SAudioDevice final
{
	std::string nodeName;
	std::string description;
	bool        isMonitor{ false };
};
} // namespace Klip::Capture
