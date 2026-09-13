#pragma once

#include "encode/format.hpp"

#include <array>
#include <string>

namespace Klip::Encode
{
struct SCapabilities final
{
	// Empty when no render node could be opened, which leaves every codec listed rather than none.
	std::string                  devicePath;
	std::array<bool, CodecCount> encodes{};
};

// Probes every render node that opens and keeps the one encoding the most of Klip's codecs -- an iGPU
// beside a dGPU takes H.264 and HEVC but not AV1. The encoder must record on this same path.
void InitializeCapabilities();

SCapabilities const& GetCapabilities();

// True when the card took this codec -- or when it took none of them, so the window is never left empty.
bool IsCodecOffered(ECodec codec);
} // namespace Klip::Encode
