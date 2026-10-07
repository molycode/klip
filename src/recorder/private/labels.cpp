#include "recorder/labels.hpp"

#include "recorder/frame_rates.hpp"

namespace Klip::Recorder
{
//////////////////////////////////////////////////////////////////////////
std::string_view GetSourceLabel(ESource source)
{
	std::string_view label;

	switch (source)
	{
		case ESource::Screen: label = "Whole screen"; break;
		case ESource::Window: label = "A window"; break;
		case ESource::Region: label = "Part of the screen"; break;
	}

	return label;
}

//////////////////////////////////////////////////////////////////////////
std::string_view GetContainerLabel(Encode::EContainer container)
{
	std::string_view label;

	switch (container)
	{
		case Encode::EContainer::Mp4:      label = "MP4"; break;
		case Encode::EContainer::Matroska: label = "Matroska (MKV)"; break;
		case Encode::EContainer::WebM:     label = "WebM"; break;
		case Encode::EContainer::Count:    break;
	}

	return label;
}

//////////////////////////////////////////////////////////////////////////
std::string_view GetCodecLabel(Encode::ECodec codec)
{
	std::string_view label;

	switch (codec)
	{
		case Encode::ECodec::H264:  label = "H.264"; break;
		case Encode::ECodec::Hevc:  label = "HEVC (H.265)"; break;
		case Encode::ECodec::Av1:   label = "AV1"; break;
		case Encode::ECodec::Count: break;
	}

	return label;
}

//////////////////////////////////////////////////////////////////////////
std::string_view GetQualityLabel(Encode::EQuality quality)
{
	std::string_view label;

	switch (quality)
	{
		case Encode::EQuality::Smallest: label = "Smallest file"; break;
		case Encode::EQuality::Smaller:  label = "Smaller file"; break;
		case Encode::EQuality::Balanced: label = "Balanced"; break;
		case Encode::EQuality::High:     label = "High"; break;
		case Encode::EQuality::Best:     label = "Best quality"; break;
		case Encode::EQuality::Count:    break;
	}

	return label;
}

//////////////////////////////////////////////////////////////////////////
std::string_view GetFrameRateLabel(uint32_t maxFrameRate)
{
	std::string_view label{ "Uncapped" };

	if (maxFrameRate == 30)
	{
		label = "Up to 30 fps";
	}
	else if (maxFrameRate == 60)
	{
		label = "Up to 60 fps";
	}

	return label;
}
} // namespace Klip::Recorder
