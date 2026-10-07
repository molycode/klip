#pragma once

#include <cstdint>

namespace Klip::Recorder
{
// The values are what Klip.conf has always stored under capture/source.
enum class ESource : uint8_t
{
	Screen,
	Window,
	Region
};
} // namespace Klip::Recorder
