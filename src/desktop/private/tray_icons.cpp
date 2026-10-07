#include "desktop/tray_icons.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace Klip::Desktop
{
namespace
{
constexpr int32_t IconSize{ 64 };
constexpr int     SamplesPerSide{ 4 };
constexpr int     NumSamples{ SamplesPerSide * SamplesPerSide };

constexpr float CentreX{ 32.0f };
constexpr float CentreY{ 32.0f };

// The frame's path, which its stroke straddles, runs from (9.5, 15.5) to (54.5, 48.5).
constexpr float FrameHalfWidth{ 22.5f };
constexpr float FrameHalfHeight{ 16.5f };
constexpr float FrameRadius{ 7.0f };
constexpr float FrameHalfStroke{ 2.5f };

// Idle draws a ring of radius 6 stroked 4 wide, recording fills the whole disc.
constexpr float DotRadius{ 8.0f };
constexpr float RingInnerRadius{ 4.0f };

struct SColour final
{
	uint8_t red;
	uint8_t green;
	uint8_t blue;
};

constexpr SColour Grey{ 190, 190, 190 };
constexpr SColour Red{ 225, 60, 60 };

//////////////////////////////////////////////////////////////////////////
// Signed distance from the frame's rounded-rectangle path, negative inside it.
float GetFrameDistance(float x, float y)
{
	float const qx{ std::abs(x - CentreX) - FrameHalfWidth + FrameRadius };
	float const qy{ std::abs(y - CentreY) - FrameHalfHeight + FrameRadius };

	return std::hypot(std::max(qx, 0.0f), std::max(qy, 0.0f)) + std::min(std::max(qx, qy), 0.0f) - FrameRadius;
}

//////////////////////////////////////////////////////////////////////////
bool IsCovered(float x, float y, bool recording)
{
	float const dotDistance{ std::hypot(x - CentreX, y - CentreY) };
	bool const  isDot{ dotDistance <= DotRadius && (recording || dotDistance >= RingInnerRadius) };

	return isDot || std::abs(GetFrameDistance(x, y)) <= FrameHalfStroke;
}

//////////////////////////////////////////////////////////////////////////
// The frame and the dot never share a pixel, so a pixel takes one colour and its coverage as alpha.
STrayImage DrawImage(bool recording)
{
	STrayImage image{ .width = IconSize, .height = IconSize, .pixels = {} };

	image.pixels.resize(static_cast<size_t>(IconSize) * IconSize * 4);

	for (int32_t y{ 0 }; y < IconSize; ++y)
	{
		for (int32_t x{ 0 }; x < IconSize; ++x)
		{
			int numCovered{ 0 };

			for (int row{ 0 }; row < SamplesPerSide; ++row)
			{
				for (int column{ 0 }; column < SamplesPerSide; ++column)
				{
					float const sampleX{ static_cast<float>(x) + (static_cast<float>(column) + 0.5f) / SamplesPerSide };
					float const sampleY{ static_cast<float>(y) + (static_cast<float>(row) + 0.5f) / SamplesPerSide };

					numCovered += IsCovered(sampleX, sampleY, recording) ? 1 : 0;
				}
			}

			if (numCovered > 0)
			{
				bool const     isDot{ std::hypot(static_cast<float>(x) + 0.5f - CentreX,
				                                 static_cast<float>(y) + 0.5f - CentreY) <= DotRadius + 1.0f };
				SColour const& colour{ (recording && isDot) ? Red : Grey };
				size_t const   offset{ (static_cast<size_t>(y) * IconSize + static_cast<size_t>(x)) * 4 };

				// Network byte order, as the StatusNotifierItem specification asks.
				image.pixels[offset] = static_cast<uint8_t>((numCovered * 255 + NumSamples / 2) / NumSamples);
				image.pixels[offset + 1] = colour.red;
				image.pixels[offset + 2] = colour.green;
				image.pixels[offset + 3] = colour.blue;
			}
		}
	}

	return image;
}
} // namespace

//////////////////////////////////////////////////////////////////////////
STrayIcons DrawTrayIcons()
{
	return STrayIcons{ .idle = DrawImage(false), .recording = DrawImage(true) };
}
} // namespace Klip::Desktop
