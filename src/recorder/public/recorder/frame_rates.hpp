#pragma once

#include <array>
#include <cstdint>

namespace Klip::Recorder
{
inline constexpr uint32_t NoFrameRateCap{ 0 };

inline constexpr std::array<uint32_t, 3> FrameRateCaps{ 30, 60, NoFrameRateCap };
} // namespace Klip::Recorder
