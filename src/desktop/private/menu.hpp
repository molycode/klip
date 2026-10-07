#pragma once

#include "bus/portal.hpp"

#include <array>
#include <cstdint>
#include <string_view>
#include <vector>

namespace Klip::Desktop
{
inline constexpr int32_t RootId{ 0 };
inline constexpr int32_t ToggleId{ 1 };
inline constexpr int32_t ShowId{ 2 };
inline constexpr int32_t SeparatorId{ 3 };
inline constexpr int32_t QuitId{ 4 };

inline constexpr std::array<int32_t, 4> MenuItemIds{ ToggleId, ShowId, SeparatorId, QuitId };

std::vector<Bus::SOption> GetMenuProperties(int32_t id, bool recording);

int AppendMenuLayout(sd_bus_message* pMessage, int32_t id, int32_t depth, bool recording);

bool FindMenuProperty(int32_t id, bool recording, std::string_view name, Bus::SOption& found);
} // namespace Klip::Desktop
