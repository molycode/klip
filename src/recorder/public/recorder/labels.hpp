#pragma once

#include "encode/format.hpp"
#include "encode/quality.hpp"
#include "recorder/source.hpp"

#include <cstdint>
#include <string_view>

namespace Klip::Recorder
{
std::string_view GetSourceLabel(ESource source);
std::string_view GetContainerLabel(Encode::EContainer container);
std::string_view GetCodecLabel(Encode::ECodec codec);
std::string_view GetQualityLabel(Encode::EQuality quality);
std::string_view GetFrameRateLabel(uint32_t maxFrameRate);
} // namespace Klip::Recorder
