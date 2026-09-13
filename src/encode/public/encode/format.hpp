#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace Klip::Encode
{
enum class ECodec : uint8_t
{
	H264,
	Hevc,
	Av1,

	Count
};

inline constexpr size_t CodecCount = static_cast<size_t>(ECodec::Count);

enum class EContainer : uint8_t
{
	Mp4,
	Matroska,
	WebM,

	Count
};

inline constexpr size_t ContainerCount = static_cast<size_t>(EContainer::Count);

struct SCodecEntry final
{
	ECodec           codec;
	std::string_view name;
	std::string_view encoder;
};

inline constexpr std::array CodecEntries
{
	SCodecEntry{ ECodec::H264, "h264", "h264_vaapi" },
	SCodecEntry{ ECodec::Hevc, "hevc", "hevc_vaapi" },
	SCodecEntry{ ECodec::Av1,  "av1",  "av1_vaapi"  }
};

//////////////////////////////////////////////////////////////////////////
inline constexpr uint32_t CodecBit(ECodec codec)
{
	return uint32_t{ 1 } << static_cast<uint32_t>(codec);
}

struct SContainerEntry final
{
	EContainer       container;
	std::string_view name;
	std::string_view extension;
	uint32_t         codecs;
	bool             carriesAudio;
};

inline constexpr uint32_t AllCodecs{ CodecBit(ECodec::H264) | CodecBit(ECodec::Hevc) |
	                                 CodecBit(ECodec::Av1) };

inline constexpr std::array ContainerEntries
{
	SContainerEntry{ EContainer::Mp4,      "mp4",      "mp4",  AllCodecs, true },
	SContainerEntry{ EContainer::Matroska, "matroska", "mkv",  AllCodecs, true },

	// WebM carries only VP8, VP9 and AV1, and Klip ships neither VP. Its audio is Opus or Vorbis, and
	// the pinned FFmpeg flags both encoders experimental, so it records silent.
	SContainerEntry{ EContainer::WebM,     "webm",     "webm", CodecBit(ECodec::Av1), false }
};

//////////////////////////////////////////////////////////////////////////
inline constexpr std::string_view GetCodecName(ECodec codec)
{
	std::string_view name{ "unknown" };

	for (SCodecEntry const& entry : CodecEntries)
	{
		if (entry.codec == codec)
		{
			name = entry.name;
		}
	}

	return name;
}

//////////////////////////////////////////////////////////////////////////
inline constexpr std::string_view GetCodecEncoder(ECodec codec)
{
	std::string_view encoder{ "" };

	for (SCodecEntry const& entry : CodecEntries)
	{
		if (entry.codec == codec)
		{
			encoder = entry.encoder;
		}
	}

	return encoder;
}

//////////////////////////////////////////////////////////////////////////
// Falls back when the name is not one Klip writes, so an edited settings file cannot leave it unset.
inline constexpr ECodec ParseCodec(std::string_view name, ECodec fallback)
{
	ECodec codec{ fallback };

	for (SCodecEntry const& entry : CodecEntries)
	{
		if (entry.name == name)
		{
			codec = entry.codec;
		}
	}

	return codec;
}

//////////////////////////////////////////////////////////////////////////
inline constexpr std::string_view GetContainerName(EContainer container)
{
	std::string_view name{ "unknown" };

	for (SContainerEntry const& entry : ContainerEntries)
	{
		if (entry.container == container)
		{
			name = entry.name;
		}
	}

	return name;
}

//////////////////////////////////////////////////////////////////////////
inline constexpr std::string_view GetContainerExtension(EContainer container)
{
	std::string_view extension{ "" };

	for (SContainerEntry const& entry : ContainerEntries)
	{
		if (entry.container == container)
		{
			extension = entry.extension;
		}
	}

	return extension;
}

//////////////////////////////////////////////////////////////////////////
inline constexpr EContainer ParseContainer(std::string_view name, EContainer fallback)
{
	EContainer container{ fallback };

	for (SContainerEntry const& entry : ContainerEntries)
	{
		if (entry.name == name)
		{
			container = entry.container;
		}
	}

	return container;
}

//////////////////////////////////////////////////////////////////////////
inline constexpr bool ContainerAccepts(EContainer container, ECodec codec)
{
	bool accepts{ false };

	for (SContainerEntry const& entry : ContainerEntries)
	{
		if (entry.container == container)
		{
			accepts = (entry.codecs & CodecBit(codec)) != 0;
		}
	}

	return accepts;
}

//////////////////////////////////////////////////////////////////////////
inline constexpr bool ContainerCarriesAudio(EContainer container)
{
	bool carries{ false };

	for (SContainerEntry const& entry : ContainerEntries)
	{
		if (entry.container == container)
		{
			carries = entry.carriesAudio;
		}
	}

	return carries;
}

//////////////////////////////////////////////////////////////////////////
// An unnamed codec would be written to the settings file as "unknown" and never read back.
consteval bool AreFormatNamesComplete()
{
	bool complete{ true };

	for (size_t index{ 0 }; index < CodecCount; ++index)
	{
		complete = complete && (GetCodecName(static_cast<ECodec>(index)) != "unknown");
		complete = complete && !GetCodecEncoder(static_cast<ECodec>(index)).empty();
	}

	for (size_t index{ 0 }; index < ContainerCount; ++index)
	{
		complete = complete && (GetContainerName(static_cast<EContainer>(index)) != "unknown");
		complete = complete && !GetContainerExtension(static_cast<EContainer>(index)).empty();
	}

	return complete;
}

static_assert(AreFormatNamesComplete(), "a codec or container is missing its name");
} // namespace Klip::Encode
