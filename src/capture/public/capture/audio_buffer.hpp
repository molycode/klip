#pragma once

#include <cstdint>

namespace Klip::Capture
{
inline constexpr uint32_t MaxAudioChannels{ 2 };

struct SAudioBuffer final
{
	float const* pPlanes[MaxAudioChannels]{ nullptr, nullptr };
	uint32_t     numFrames{ 0 };
	uint32_t     numChannels{ 0 };
	uint64_t     timestampNs{ 0 };
};
} // namespace Klip::Capture
