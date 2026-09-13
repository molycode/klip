#pragma once

#include "capture/frame.hpp"
#include "encode/format.hpp"
#include "encode/quality.hpp"

#include <array>
#include <cstdint>
#include <string>

namespace Klip::Encode
{
// A zero width or height means the whole frame.
struct SRegion final
{
	uint32_t x{ 0 };
	uint32_t y{ 0 };
	uint32_t width{ 0 };
	uint32_t height{ 0 };
};

struct SAudioSettings final
{
	uint32_t sampleRate{ 48000 };
	uint32_t numChannels{ 2 };

	// Zero records no sound. Two are mixed into one track.
	uint32_t numSources{ 0 };

	// Klip's own trim per source, linear. The system's own volumes are left alone: a sink's does not
	// reach its monitor at all, and a source's belongs to every other application too.
	std::array<float, 2> gain{ 1.0f, 1.0f };

	EQuality quality{ EQuality::High };
};

struct SSettings final
{
	std::string outputPath;
	uint64_t    modifier{ 0 };
	uint32_t    width{ 0 };
	uint32_t    height{ 0 };
	SRegion     region;

	// The container comes from outputPath's extension, which is what the muxer is chosen by.
	ECodec   codec{ ECodec::H264 };
	EQuality quality{ EQuality::Balanced };

	// Rate control reasons about this, so it has to be what the stream will actually deliver at most.
	// Zero leaves the encoder's own nominal in place.
	uint32_t maxFrameRate{ 0 };

	SAudioSettings audio;

	// The recording's zero point, taken from the first video frame so both streams share an epoch.
	uint64_t firstTimestampNs{ 0 };

	Capture::EPixelFormat sourceFormat{ Capture::EPixelFormat::BGRx };
	Capture::EFrameMemory memory{ Capture::EFrameMemory::Mapped };
};
} // namespace Klip::Encode
