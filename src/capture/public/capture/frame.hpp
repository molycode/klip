#pragma once

#include <cstdint>

namespace Klip::Capture
{
inline constexpr uint32_t MaxPlanes{ 4 };

enum class EPixelFormat : uint8_t
{
	BGRx,
	RGBx,
	BGRA,
	RGBA
};

enum class EFrameMemory : uint8_t
{
	Mapped,
	DmaBuf
};

struct SPlane final
{
	int      fd{ -1 };
	uint32_t offset{ 0 };
	uint32_t stride{ 0 };
	uint32_t size{ 0 };
};

struct SFrame final
{
	uint8_t const* pPixels{ nullptr };
	SPlane         planes[MaxPlanes]{};
	uint64_t       modifier{ 0 };
	uint64_t       timestampNs{ 0 };
	// A window capture is padded out to a larger buffer, and the padding is not black by accident --
	// it is simply not part of the frame.
	uint32_t       cropX{ 0 };
	uint32_t       cropY{ 0 };
	uint32_t       cropWidth{ 0 };
	uint32_t       cropHeight{ 0 };

	uint32_t       numPlanes{ 0 };
	uint32_t       stride{ 0 };
	uint32_t       width{ 0 };
	uint32_t       height{ 0 };
	EPixelFormat   format{ EPixelFormat::BGRx };
	EFrameMemory   memory{ EFrameMemory::Mapped };
};
} // namespace Klip::Capture
