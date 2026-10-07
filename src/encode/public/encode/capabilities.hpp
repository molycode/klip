#pragma once

#include "encode/format.hpp"

#include <array>
#include <string>

namespace Klip::Encode
{
struct SCapabilities final
{
	std::string                  devicePath;
	std::array<bool, CodecCount> encodes{};
};

void InitializeCapabilities();

SCapabilities const& GetCapabilities();

bool IsCodecOffered(ECodec codec);
} // namespace Klip::Encode
