#pragma once

#include <cstdint>

namespace Klip::Recorder
{
enum class EState : uint8_t
{
	Idle,
	Starting,
	Recording
};
} // namespace Klip::Recorder
