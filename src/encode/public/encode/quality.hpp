#pragma once

#include "encode/format.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace Klip::Encode
{
enum class EQuality : uint8_t
{
	Smallest,
	Smaller,
	Balanced,
	High,
	Best,

	Count
};

inline constexpr size_t QualityCount = static_cast<size_t>(EQuality::Count);

// Values for AVCodecContext::global_quality, which every VAAPI encoder reads once rc_mode is CQP.
// H.264 and HEVC clamp it to 1-51 internally; AV1 carries a q_index instead and clamps to 1-255.
// Measured on one AMD card under VAAPI, so a level costs about the same wherever it lands: against
// H.264 the HEVC column comes out 11-19% smaller and the AV1 column 23-26%, which is the file size a
// user changing codec is really asking about.
struct SCodecQuality final
{
	int globalQuality;

	// Bits per pixel per frame, times ten thousand. Measured on one AMD card under VAAPI; content
	// moves it by half again either way, which is why the window says "up to".
	int bitsPerPixel;
};

struct SQualityEntry final
{
	EQuality         quality;
	std::string_view name;
	SCodecQuality    h264;
	SCodecQuality    hevc;
	SCodecQuality    av1;

	// AAC, which is the only audio codec MP4 and Matroska get from the pinned LGPL FFmpeg.
	int audioBitsPerSecond;
};

inline constexpr std::array QualityEntries
{
	SQualityEntry{ .quality = EQuality::Smallest, .name = "smallest",
	               .h264 = { 34, 27 }, .hevc = { 36, 23 }, .av1 = { 196, 20 },
	               .audioBitsPerSecond = 64000 },
	SQualityEntry{ .quality = EQuality::Smaller,  .name = "smaller",
	               .h264 = { 29, 37 }, .hevc = { 32, 29 }, .av1 = { 160, 26 },
	               .audioBitsPerSecond = 96000 },
	SQualityEntry{ .quality = EQuality::Balanced, .name = "balanced",
	               .h264 = { 25, 49 }, .hevc = { 28, 37 }, .av1 = { 124, 34 },
	               .audioBitsPerSecond = 128000 },
	SQualityEntry{ .quality = EQuality::High,     .name = "high",
	               .h264 = { 21, 61 }, .hevc = { 24, 47 }, .av1 = {  76, 44 },
	               .audioBitsPerSecond = 192000 },
	SQualityEntry{ .quality = EQuality::Best,     .name = "best",
	               .h264 = { 17, 78 }, .hevc = { 20, 60 }, .av1 = {  52, 54 },
	               .audioBitsPerSecond = 256000 }
};

//////////////////////////////////////////////////////////////////////////
inline constexpr std::string_view GetQualityName(EQuality quality)
{
	std::string_view name{ "unknown" };

	for (SQualityEntry const& entry : QualityEntries)
	{
		if (entry.quality == quality)
		{
			name = entry.name;
		}
	}

	return name;
}

//////////////////////////////////////////////////////////////////////////
inline constexpr EQuality ParseQuality(std::string_view name, EQuality fallback)
{
	EQuality quality{ fallback };

	for (SQualityEntry const& entry : QualityEntries)
	{
		if (entry.name == name)
		{
			quality = entry.quality;
		}
	}

	return quality;
}

//////////////////////////////////////////////////////////////////////////
inline constexpr int GetAudioBitsPerSecond(EQuality quality)
{
	int bitsPerSecond{ 0 };

	for (SQualityEntry const& entry : QualityEntries)
	{
		if (entry.quality == quality)
		{
			bitsPerSecond = entry.audioBitsPerSecond;
		}
	}

	return bitsPerSecond;
}

//////////////////////////////////////////////////////////////////////////
inline constexpr SCodecQuality ResolveCodecQuality(ECodec codec, EQuality quality)
{
	SCodecQuality resolved{ 0, 0 };

	for (SQualityEntry const& entry : QualityEntries)
	{
		if (entry.quality == quality)
		{
			switch (codec)
			{
				case ECodec::H264:  resolved = entry.h264; break;
				case ECodec::Hevc:  resolved = entry.hevc; break;
				case ECodec::Av1:   resolved = entry.av1;  break;
				case ECodec::Count: break;
			}
		}
	}

	return resolved;
}

//////////////////////////////////////////////////////////////////////////
inline constexpr int ResolveGlobalQuality(ECodec codec, EQuality quality)
{
	return ResolveCodecQuality(codec, quality).globalQuality;
}

//////////////////////////////////////////////////////////////////////////
// An upper bound: the rate assumed here is the ceiling the compositor will ever deliver, and a screen
// only produces frames where it changes.
inline constexpr uint64_t EstimateBitsPerSecond(ECodec codec, EQuality quality, uint32_t width,
                                                uint32_t height, uint32_t framesPerSecond)
{
	uint64_t const bitsPerPixel{ static_cast<uint64_t>(ResolveCodecQuality(codec, quality).bitsPerPixel) };

	return (bitsPerPixel * width * height * framesPerSecond) / 10000;
}

//////////////////////////////////////////////////////////////////////////
// Zero reads as "unset" inside the encoder, which silently swaps in the codec's own default.
consteval bool IsQualityLadderComplete()
{
	bool complete{ true };

	for (size_t step{ 0 }; step < QualityCount; ++step)
	{
		EQuality const quality{ static_cast<EQuality>(step) };

		complete = complete && (GetQualityName(quality) != "unknown");
		complete = complete && (GetAudioBitsPerSecond(quality) > 0);

		for (size_t index{ 0 }; index < CodecCount; ++index)
		{
			complete = complete && (ResolveGlobalQuality(static_cast<ECodec>(index), quality) > 0);
			complete = complete &&
			           (ResolveCodecQuality(static_cast<ECodec>(index), quality).bitsPerPixel > 0);
		}
	}

	return complete;
}

static_assert(IsQualityLadderComplete(), "a quality level is missing a name, a codec's value or its audio rate");
} // namespace Klip::Encode
