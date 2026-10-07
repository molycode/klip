#include "desktop/tray.hpp"
#include "desktop/tray_icons.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>

using namespace Klip;

namespace
{
using SPixel = std::array<uint8_t, 4>;

constexpr SPixel Clear{ 0, 0, 0, 0 };
constexpr SPixel Grey{ 255, 190, 190, 190 };
constexpr SPixel Red{ 255, 225, 60, 60 };

//////////////////////////////////////////////////////////////////////////
// As the bytes go onto the bus: alpha, red, green, blue.
SPixel GetPixel(Desktop::STrayImage const& image, int32_t x, int32_t y)
{
	size_t const offset{ (static_cast<size_t>(y) * static_cast<size_t>(image.width) + static_cast<size_t>(x)) * 4 };

	return { image.pixels[offset], image.pixels[offset + 1], image.pixels[offset + 2], image.pixels[offset + 3] };
}
} // namespace

//////////////////////////////////////////////////////////////////////////
TEST(TrayIcons, AreSixtyFourPixelsSquare)
{
	Desktop::STrayIcons const icons{ Desktop::DrawTrayIcons() };

	for (Desktop::STrayImage const* pImage : { &icons.idle, &icons.recording })
	{
		EXPECT_EQ(pImage->width, 64);
		EXPECT_EQ(pImage->height, 64);
		EXPECT_EQ(pImage->pixels.size(), size_t{ 64 } * 64 * 4);
	}
}

//////////////////////////////////////////////////////////////////////////
TEST(TrayIcons, CornerIsTransparent)
{
	EXPECT_EQ(GetPixel(Desktop::DrawTrayIcons().idle, 0, 0), Clear);
}

//////////////////////////////////////////////////////////////////////////
TEST(TrayIcons, FrameIsGrey)
{
	Desktop::STrayImage const idle{ Desktop::DrawTrayIcons().idle };

	EXPECT_EQ(GetPixel(idle, 9, 32), Grey);
	EXPECT_EQ(GetPixel(idle, 32, 15), Grey);
}

//////////////////////////////////////////////////////////////////////////
TEST(TrayIcons, IdleRingIsGreyAroundAClearCentre)
{
	Desktop::STrayImage const idle{ Desktop::DrawTrayIcons().idle };

	EXPECT_EQ(GetPixel(idle, 38, 32), Grey);
	EXPECT_EQ(GetPixel(idle, 32, 32), Clear);
}

//////////////////////////////////////////////////////////////////////////
TEST(TrayIcons, RecordingCentreIsRed)
{
	EXPECT_EQ(GetPixel(Desktop::DrawTrayIcons().recording, 32, 32), Red);
}

//////////////////////////////////////////////////////////////////////////
TEST(TrayIcons, RecordingKeepsTheGreyFrame)
{
	EXPECT_EQ(GetPixel(Desktop::DrawTrayIcons().recording, 9, 32), Grey);
}

//////////////////////////////////////////////////////////////////////////
TEST(TrayIcons, EdgesAreAntiAliased)
{
	SPixel const edge{ GetPixel(Desktop::DrawTrayIcons().recording, 37, 26) };

	EXPECT_GT(edge[0], 0);
	EXPECT_LT(edge[0], 255);
}
