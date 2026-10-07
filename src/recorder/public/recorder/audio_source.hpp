#pragma once

#include <cstddef>
#include <cstdint>

namespace Klip::Recorder
{
enum class EAudioSource : uint8_t
{
	System,
	Microphone
};

inline constexpr size_t NumAudioSources{ 2 };
} // namespace Klip::Recorder
