#include "level_meter.hpp"

#include "format_to.hpp"
#include "recorder/decibels.hpp"
#include "recorder/level_ballistics.hpp"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <string_view>

namespace Klip
{
namespace
{
constexpr uint32_t NumChannels{ 2 };

constexpr float BarHeight{ 10.0f };
constexpr float BarSpacing{ 4.0f };
constexpr float LabelWidth{ 14.0f };
constexpr float MarkerWidth{ 2.0f };
constexpr float ScaleGap{ 3.0f };
constexpr float TickHeight{ 3.0f };

constexpr float    TickStepDecibels{ 6.0f };
constexpr uint32_t NumTicks{ 10 };
constexpr uint32_t LabelEveryNthTick{ 2 };

constexpr float LabelFontSize{ 14.0f };
constexpr float ScaleFontSize{ 13.0f };

constexpr ImU32 LabelColor{ IM_COL32(140, 140, 140, 255) };
constexpr ImU32 ScaleColor{ IM_COL32(110, 110, 110, 255) };
constexpr ImU32 TroughColor{ IM_COL32(40, 40, 40, 255) };
constexpr ImU32 HoldColor{ IM_COL32(235, 235, 235, 255) };
constexpr ImU32 ClipColor{ IM_COL32(240, 60, 50, 255) };
constexpr ImU32 UnavailableColor{ IM_COL32(222, 120, 100, 255) };

struct SGradientStop final
{
	float position;
	ImU32 color;
};

// Anchored to the scale rather than to the current level, so a colour always means the same loudness.
constexpr std::array GradientStops
{
	SGradientStop{ 0.00f, IM_COL32(60, 185, 105, 255) },
	SGradientStop{ 0.62f, IM_COL32(105, 205, 110, 255) },
	SGradientStop{ 0.74f, IM_COL32(225, 190, 75, 255) },
	SGradientStop{ 0.88f, IM_COL32(230, 140, 60, 255) },
	SGradientStop{ 1.00f, IM_COL32(222, 60, 52, 255) }
};

//////////////////////////////////////////////////////////////////////////
float ToPosition(float level)
{
	float const decibels{ std::clamp(Recorder::ToDecibels(level), Recorder::FloorDecibels, 0.0f) };

	return 1.0f - decibels / Recorder::FloorDecibels;
}

//////////////////////////////////////////////////////////////////////////
ImU32 LerpColor(ImU32 from, ImU32 to, float t)
{
	ImVec4 const a{ ImGui::ColorConvertU32ToFloat4(from) };
	ImVec4 const b{ ImGui::ColorConvertU32ToFloat4(to) };

	return ImGui::ColorConvertFloat4ToU32(
		ImVec4{ a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t, a.w + (b.w - a.w) * t });
}

//////////////////////////////////////////////////////////////////////////
// Segment by segment, each one cut where the fill ends, so the colours stay where the scale puts them.
void DrawFill(ImDrawList& drawList, ImVec2 const& min, float width, float height, float fill)
{
	for (size_t index{ 1 }; index < GradientStops.size(); ++index)
	{
		SGradientStop const& from{ GradientStops[index - 1] };
		SGradientStop const& to{ GradientStops[index] };
		float const          end{ std::min(to.position, fill) };

		if (end > from.position)
		{
			ImU32 const endColor{ LerpColor(from.color, to.color, (end - from.position) / (to.position - from.position)) };

			drawList.AddRectFilledMultiColor(ImVec2{ min.x + width * from.position, min.y },
			                                 ImVec2{ min.x + width * end, min.y + height }, from.color, endColor,
			                                 endColor, from.color);
		}
	}
}

//////////////////////////////////////////////////////////////////////////
// Even ticks are honest only while the bar stays linear in decibels.
void DrawScale(ImDrawList& drawList, float left, float width, float top, float scale)
{
	std::array<char, 16> text{};

	for (uint32_t step{ 0 }; step <= NumTicks; ++step)
	{
		float const decibels{ Recorder::FloorDecibels + static_cast<float>(step) * TickStepDecibels };
		float const x{ left + width * (1.0f - decibels / Recorder::FloorDecibels) };

		drawList.AddLine(ImVec2{ x, top + ScaleGap * scale }, ImVec2{ x, top + (ScaleGap + TickHeight) * scale },
		                 ScaleColor, scale);

		if (step % LabelEveryNthTick == 0)
		{
			std::string_view const label{ step == NumTicks ? FormatTo(text, "0 dBFS")
			                                               : FormatTo(text, "{}", static_cast<int>(decibels)) };
			float const            labelWidth{ ImGui::CalcTextSize(label.data(), label.data() + label.size()).x };
			float                  labelX{ x - labelWidth / 2.0f };

			if (step == 0)
			{
				labelX = x;
			}
			else if (step == NumTicks)
			{
				labelX = x - labelWidth;
			}

			drawList.AddText(ImVec2{ labelX, top + (ScaleGap + TickHeight + 1.0f) * scale }, ScaleColor,
			                 label.data(), label.data() + label.size());
		}
	}
}
} // namespace

//////////////////////////////////////////////////////////////////////////
void DrawLevelMeter(Recorder::CLevelBallistics const& ballistics, float scale)
{
	ImDrawList& drawList{ *ImGui::GetWindowDrawList() };
	ImVec2 const origin{ ImGui::GetCursorScreenPos() };
	float const  width{ ImGui::GetContentRegionAvail().x };
	float const  barsHeight{ static_cast<float>(NumChannels) * (BarHeight + BarSpacing) * scale - BarSpacing * scale };

	ImGui::PushFont(nullptr, ScaleFontSize);

	float const scaleHeight{ (ScaleGap + TickHeight + 1.0f) * scale + ImGui::GetTextLineHeight() };

	ImGui::PopFont();

	if (ballistics.IsUnavailable())
	{
		ImGui::PushFont(nullptr, LabelFontSize);
		drawList.AddText(ImVec2{ origin.x, origin.y + (barsHeight + scaleHeight - ImGui::GetTextLineHeight()) / 2.0f },
		                 UnavailableColor, "unavailable \xe2\x80\x94 pick another device");
		ImGui::PopFont();
	}
	else
	{
		float const barLeft{ origin.x + LabelWidth * scale };
		float const barWidth{ width - LabelWidth * scale };
		float const markerWidth{ MarkerWidth * scale };

		ImGui::PushFont(nullptr, LabelFontSize);

		for (uint32_t channel{ 0 }; channel < NumChannels; ++channel)
		{
			float const  top{ origin.y + static_cast<float>(channel) * (BarHeight + BarSpacing) * scale };
			float const  barHeight{ BarHeight * scale };
			ImVec2 const barMin{ barLeft, top };
			char const*  pLabel{ channel == 0 ? "L" : "R" };

			drawList.AddText(ImVec2{ origin.x, top + (barHeight - ImGui::GetTextLineHeight()) / 2.0f }, LabelColor,
			                 pLabel);
			drawList.AddRectFilled(barMin, ImVec2{ barLeft + barWidth, top + barHeight }, TroughColor);

			float const fill{ ToPosition(ballistics.GetLevel(channel)) };

			DrawFill(drawList, barMin, barWidth, barHeight, fill);

			float const filled{ barWidth * fill };
			float const marker{ std::min(barWidth * ToPosition(ballistics.GetHold(channel)), barWidth - markerWidth) };

			// Only while it is ahead of the bar: a steady signal holds its own peak, and a marker sitting on the
			// tip marks nothing -- on a quiet input it is all there is to see.
			if (marker > filled + markerWidth)
			{
				drawList.AddRectFilled(ImVec2{ barLeft + marker, top },
				                       ImVec2{ barLeft + marker + markerWidth, top + barHeight }, HoldColor);
			}

			if (ballistics.IsClipped(channel))
			{
				drawList.AddRectFilled(ImVec2{ barLeft + barWidth - markerWidth, top },
				                       ImVec2{ barLeft + barWidth, top + barHeight }, ClipColor);
			}
		}

		ImGui::PopFont();
		ImGui::PushFont(nullptr, ScaleFontSize);
		DrawScale(drawList, barLeft, barWidth - scale, origin.y + barsHeight, scale);
		ImGui::PopFont();
	}

	ImGui::Dummy(ImVec2{ width, barsHeight + scaleHeight });
}
} // namespace Klip
