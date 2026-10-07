#pragma once

#include <cstdint>

namespace Klip::Recorder
{
enum class EState : uint8_t
{
	Idle,
	Starting,

	// From the portal's grant, which comes before the first frame is encoded -- and without one, if the
	// encoder cannot start.
	Recording
};
} // namespace Klip::Recorder
