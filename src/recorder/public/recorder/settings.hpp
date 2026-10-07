#pragma once

#include "encode/format.hpp"
#include "encode/quality.hpp"
#include "recorder/audio_choice.hpp"
#include "recorder/audio_source.hpp"
#include "recorder/frame_rates.hpp"
#include "recorder/source.hpp"

#include <array>
#include <cstdint>
#include <string>

namespace Klip::Recorder
{
struct SSettings final
{
	std::string        directory;
	ESource            source{ ESource::Screen };
	bool               rememberWindow{ false };
	Encode::EContainer container{ Encode::EContainer::Mp4 };
	Encode::ECodec     codec{ Encode::ECodec::H264 };
	Encode::EQuality   quality{ Encode::EQuality::Balanced };
	uint32_t           maxFrameRate{ NoFrameRateCap };

	std::array<SAudioChoice, NumAudioSources> audio;
	Encode::EQuality                          audioQuality{ Encode::EQuality::High };

	std::string screenToken;
	std::string windowToken;

	bool operator==(SSettings const&) const = default;
};
} // namespace Klip::Recorder
