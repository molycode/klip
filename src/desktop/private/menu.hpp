#pragma once

#include "bus/portal.hpp"

#include <array>
#include <cstdint>
#include <string_view>
#include <vector>

// The com.canonical.dbusmenu side of the tray: which items there are and how they are marshalled.
namespace Klip::Desktop
{
inline constexpr int32_t RootId{ 0 };
inline constexpr int32_t ToggleId{ 1 };
inline constexpr int32_t ShowId{ 2 };
inline constexpr int32_t SeparatorId{ 3 };
inline constexpr int32_t QuitId{ 4 };

inline constexpr std::array<int32_t, 4> MenuItemIds{ ToggleId, ShowId, SeparatorId, QuitId };

std::vector<Bus::SOption> GetMenuProperties(int32_t id, bool recording);

// (ia{sv}av), with the root's items as children when depth allows.
int AppendMenuLayout(sd_bus_message* pMessage, int32_t id, int32_t depth, bool recording);

// False when the item has no property of that name.
bool FindMenuProperty(int32_t id, bool recording, std::string_view name, Bus::SOption& found);
} // namespace Klip::Desktop
