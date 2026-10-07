#include "fonts.hpp"

#include "embedded_fonts.hpp"
#include "log.hpp"
#include "theme.hpp"

#include <imgui.h>

#include <array>

namespace Klip
{
namespace
{
// Font Awesome also maps glyphs onto Latin code points; only its private-use icons may fill in for the text font.
constexpr std::array<ImWchar, 5> IconExcludedRanges{ 0x0001, 0xDFFF, 0xF900, 0xFFFF, 0 };
} // namespace

//////////////////////////////////////////////////////////////////////////
// AddFontFromMemoryTTF takes a mutable pointer but only reads the data when the atlas does not own it.
bool LoadFonts()
{
	ImGuiIO const& io{ ImGui::GetIO() };
	ImFontConfig   textConfig{};

	textConfig.FontDataOwnedByAtlas = false;

	bool const hasTextFont{ io.Fonts->AddFontFromMemoryTTF(const_cast<unsigned char*>(Embedded::RobotoMedium.data()),
	                                                        static_cast<int>(Embedded::RobotoMedium.size()),
	                                                        BaseFontSize, &textConfig) != nullptr };

	if (hasTextFont)
	{
		ImFontConfig iconConfig{};

		iconConfig.FontDataOwnedByAtlas = false;
		iconConfig.MergeMode = true;
		iconConfig.PixelSnapH = true;
		iconConfig.GlyphMinAdvanceX = BaseFontSize;
		iconConfig.GlyphExcludeRanges = IconExcludedRanges.data();

		if (io.Fonts->AddFontFromMemoryTTF(const_cast<unsigned char*>(Embedded::FontAwesomeSolid.data()),
		                                   static_cast<int>(Embedded::FontAwesomeSolid.size()), BaseFontSize,
		                                   &iconConfig) == nullptr)
		{
			gLog.Warning("Cannot load the embedded icon font, so icons show as missing glyphs");
		}
	}
	else
	{
		gLog.Error("Cannot load the embedded UI font");
	}

	return hasTextFont;
}
} // namespace Klip
