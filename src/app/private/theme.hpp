#pragma once

struct ImVec4;

namespace Klip
{
inline constexpr float BaseFontSize{ 16.0f };

void ApplyTheme(float scale);

ImVec4 GetBackgroundColor();
ImVec4 GetAccentColor();
ImVec4 GetErrorColor();
} // namespace Klip
