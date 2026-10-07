#pragma once

#include "capture/frame.hpp"
#include "encode/format.hpp"
#include "encode/quality.hpp"

#include <array>
#include <cstdint>
#include <string>

namespace Klip::Encode
{
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

	uint32_t numSources{ 0 };

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

	ECodec   codec{ ECodec::H264 };
	EQuality quality{ EQuality::Balanced };

	uint32_t maxFrameRate{ 0 };

	SAudioSettings audio;

	uint64_t firstTimestampNs{ 0 };

	Capture::EPixelFormat sourceFormat{ Capture::EPixelFormat::BGRx };
	Capture::EFrameMemory memory{ Capture::EFrameMemory::Mapped };
};
} // namespace Klip::Encode
