#pragma once

#include "recorder/settings.hpp"

#include <cstdint>
#include <string>

namespace Klip::Config
{
struct SSettingsDocument final
{
	Recorder::SSettings settings;
	uint32_t            version{ 0 };
	uint32_t            numInvalid{ 0 };
	std::string         firstInvalidPath;
};
} // namespace Klip::Config
