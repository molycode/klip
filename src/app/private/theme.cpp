#include "theme.hpp"

#include <imgui.h>

namespace Klip
{
namespace
{
constexpr ImVec4 Background{ 0.086f, 0.090f, 0.102f, 1.0f };
constexpr ImVec4 Panel{ 0.106f, 0.110f, 0.125f, 1.0f };
constexpr ImVec4 Popup{ 0.196f, 0.204f, 0.231f, 0.98f };
constexpr ImVec4 Border{ 0.173f, 0.180f, 0.200f, 1.0f };
constexpr ImVec4 Frame{ 0.141f, 0.149f, 0.169f, 1.0f };
constexpr ImVec4 FrameHovered{ 0.180f, 0.188f, 0.212f, 1.0f };
constexpr ImVec4 FrameActive{ 0.220f, 0.227f, 0.255f, 1.0f };
constexpr ImVec4 Text{ 0.902f, 0.886f, 0.847f, 1.0f };
constexpr ImVec4 TextDisabled{ 0.541f, 0.525f, 0.486f, 1.0f };
constexpr ImVec4 Amber{ 0.851f, 0.604f, 0.169f, 1.0f };
constexpr ImVec4 AmberBright{ 0.878f, 0.659f, 0.227f, 1.0f };

//////////////////////////////////////////////////////////////////////////
constexpr ImVec4 WithAlpha(ImVec4 const& color, float alpha)
{
	return ImVec4{ color.x, color.y, color.z, alpha };
}

//////////////////////////////////////////////////////////////////////////
void ApplyColors(ImGuiStyle& style)
{
	ImVec4* const pColors{ style.Colors };

	pColors[ImGuiCol_Text]                      = Text;
	pColors[ImGuiCol_TextDisabled]              = TextDisabled;
	pColors[ImGuiCol_WindowBg]                  = Background;
	pColors[ImGuiCol_ChildBg]                   = Panel;
	pColors[ImGuiCol_PopupBg]                   = Popup;
	pColors[ImGuiCol_Border]                    = Border;
	pColors[ImGuiCol_FrameBg]                   = Frame;
	pColors[ImGuiCol_FrameBgHovered]            = FrameHovered;
	pColors[ImGuiCol_FrameBgActive]             = FrameActive;
	pColors[ImGuiCol_TitleBg]                   = Panel;
	pColors[ImGuiCol_TitleBgActive]             = Panel;
	pColors[ImGuiCol_MenuBarBg]                 = Panel;
	pColors[ImGuiCol_ScrollbarBg]               = Background;
	pColors[ImGuiCol_ScrollbarGrab]             = FrameHovered;
	pColors[ImGuiCol_ScrollbarGrabHovered]      = FrameActive;
	pColors[ImGuiCol_ScrollbarGrabActive]       = Amber;
	pColors[ImGuiCol_CheckMark]                 = AmberBright;
	pColors[ImGuiCol_SliderGrab]                = Amber;
	pColors[ImGuiCol_SliderGrabActive]          = AmberBright;
	pColors[ImGuiCol_Button]                    = Frame;
	pColors[ImGuiCol_ButtonHovered]             = FrameHovered;
	pColors[ImGuiCol_ButtonActive]              = WithAlpha(Amber, 0.75f);
	pColors[ImGuiCol_Header]                    = WithAlpha(Amber, 0.28f);
	pColors[ImGuiCol_HeaderHovered]             = WithAlpha(Amber, 0.40f);
	pColors[ImGuiCol_HeaderActive]              = WithAlpha(Amber, 0.55f);
	pColors[ImGuiCol_Separator]                 = Border;
	pColors[ImGuiCol_SeparatorHovered]          = WithAlpha(Amber, 0.60f);
	pColors[ImGuiCol_SeparatorActive]           = Amber;
	pColors[ImGuiCol_ResizeGrip]                = WithAlpha(Amber, 0.20f);
	pColors[ImGuiCol_ResizeGripHovered]         = WithAlpha(Amber, 0.60f);
	pColors[ImGuiCol_ResizeGripActive]          = Amber;
	pColors[ImGuiCol_Tab]                       = Frame;
	pColors[ImGuiCol_TabHovered]                = WithAlpha(Amber, 0.40f);
	pColors[ImGuiCol_TabSelected]               = WithAlpha(Amber, 0.30f);
	pColors[ImGuiCol_TabSelectedOverline]       = Amber;
	pColors[ImGuiCol_TabDimmed]                 = Frame;
	pColors[ImGuiCol_TabDimmedSelected]         = WithAlpha(Amber, 0.18f);
	pColors[ImGuiCol_TabDimmedSelectedOverline] = WithAlpha(Amber, 0.50f);
	pColors[ImGuiCol_PlotLines]                 = TextDisabled;
	pColors[ImGuiCol_PlotLinesHovered]          = AmberBright;
	pColors[ImGuiCol_PlotHistogram]             = Amber;
	pColors[ImGuiCol_PlotHistogramHovered]      = AmberBright;
	pColors[ImGuiCol_TableHeaderBg]             = Panel;
	pColors[ImGuiCol_TableBorderStrong]         = Border;
	pColors[ImGuiCol_TableBorderLight]          = Frame;
	pColors[ImGuiCol_TableRowBg]                = WithAlpha(Background, 0.0f);
	pColors[ImGuiCol_TableRowBgAlt]             = WithAlpha(Text, 0.025f);
	pColors[ImGuiCol_TextSelectedBg]            = WithAlpha(Amber, 0.35f);
	pColors[ImGuiCol_TextLink]                  = AmberBright;
	pColors[ImGuiCol_InputTextCursor]           = Text;
	pColors[ImGuiCol_DragDropTarget]            = AmberBright;
	pColors[ImGuiCol_NavCursor]                 = Amber;
	pColors[ImGuiCol_NavWindowingHighlight]     = WithAlpha(Text, 0.70f);
	pColors[ImGuiCol_NavWindowingDimBg]         = WithAlpha(Background, 0.60f);
	pColors[ImGuiCol_ModalWindowDimBg]          = WithAlpha(Background, 0.70f);
}

//////////////////////////////////////////////////////////////////////////
void ApplyMetrics(ImGuiStyle& style)
{
	style.WindowPadding     = ImVec2{ 12.0f, 10.0f };
	style.FramePadding      = ImVec2{ 8.0f, 5.0f };
	style.CellPadding       = ImVec2{ 8.0f, 5.0f };
	style.ItemSpacing       = ImVec2{ 8.0f, 6.0f };
	style.ItemInnerSpacing  = ImVec2{ 6.0f, 4.0f };
	style.ScrollbarSize     = 12.0f;
	style.GrabMinSize       = 10.0f;
	style.WindowBorderSize  = 0.0f;
	style.ChildBorderSize   = 1.0f;
	style.PopupBorderSize   = 1.0f;
	style.FrameBorderSize   = 0.0f;
	style.WindowRounding    = 6.0f;
	style.ChildRounding     = 6.0f;
	style.FrameRounding     = 4.0f;
	style.PopupRounding     = 6.0f;
	style.ScrollbarRounding = 6.0f;
	style.GrabRounding      = 4.0f;
	style.TabRounding       = 4.0f;
}
} // namespace

//////////////////////////////////////////////////////////////////////////
// From scratch: ScaleAllSizes multiplies, so scaling an already scaled style compounds.
void ApplyTheme(float scale)
{
	ImGuiStyle style{};

	ApplyColors(style);
	ApplyMetrics(style);
	style.ScaleAllSizes(scale);
	style.FontSizeBase = BaseFontSize;
	style.FontScaleDpi = scale;

	ImGui::GetStyle() = style;
}

//////////////////////////////////////////////////////////////////////////
ImVec4 GetBackgroundColor()
{
	return Background;
}

//////////////////////////////////////////////////////////////////////////
ImVec4 GetAccentColor()
{
	return Amber;
}
} // namespace Klip
