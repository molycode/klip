#include "encode/encoder.hpp"

#include "encode/capabilities.hpp"
#include "log.hpp"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavfilter/avfilter.h>
#include <libavfilter/buffersink.h>
#include <libavfilter/buffersrc.h>
#include <libavformat/avformat.h>
#include <libavutil/audio_fifo.h>
#include <libavutil/hwcontext.h>
#include <libavutil/hwcontext_drm.h>
#include <libavutil/hwcontext_vaapi.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#include <libswscale/swscale.h>
}

#include <libdrm/drm_fourcc.h>
#include <tge/profiling/profiling.hpp>
#include <va/va.h>

#include <cstdio>
#include <string>
#include <string_view>

namespace Klip::Encode
{
namespace
{
constexpr int      GopSize{ 60 };
constexpr int      NominalFrameRate{ 60 };
constexpr int64_t  MicrosecondsPerSecond{ 1000000 };

// A graph quantum change alone shows up as one buffer of apparent drift -- measured at 21 ms -- so only
// a hole several times larger than that is a real one worth filling.
constexpr int64_t GapThresholdNs{ 100000000 };

// Past this a source has stopped rather than jittered: the mix has to go on without it, or every
// other fifo grows for the rest of the recording -- 46 MiB per minute measured.
constexpr int64_t StallToleranceNs{ 1000000000 };

constexpr int64_t NominalFrameMicroseconds{ 16667 };

// 4:2:0 chroma is half resolution in both axes, so an odd size or origin has no chroma sample to sit on.
uint32_t RoundDownToEven(uint32_t value)
{
	return value & ~1u;
}

SRegion ResolveRegion(SSettings const& settings)
{
	SRegion region{ settings.region };

	if (region.width == 0 || region.height == 0 || region.x + region.width > settings.width ||
	    region.y + region.height > settings.height)
	{
		region = SRegion{ 0, 0, settings.width, settings.height };
	}

	// Measured: the driver rounds an odd origin down itself and says nothing, so a region dragged from
	// an odd pixel recorded one pixel across from the one that was chosen.
	region.x = RoundDownToEven(region.x);
	region.y = RoundDownToEven(region.y);
	region.width = RoundDownToEven(region.width);
	region.height = RoundDownToEven(region.height);

	return region;
}

uint32_t ToDrmFourcc(Capture::EPixelFormat format)
{
	uint32_t fourcc{ DRM_FORMAT_XRGB8888 };

	// SPA names the byte order; DRM names the little-endian word, so BGRx is XRGB8888.
	switch (format)
	{
		case Capture::EPixelFormat::BGRx: fourcc = DRM_FORMAT_XRGB8888; break;
		case Capture::EPixelFormat::RGBx: fourcc = DRM_FORMAT_XBGR8888; break;
		case Capture::EPixelFormat::BGRA: fourcc = DRM_FORMAT_ARGB8888; break;
		case Capture::EPixelFormat::RGBA: fourcc = DRM_FORMAT_ABGR8888; break;
	}

	return fourcc;
}

AVPixelFormat ToAvFormat(Capture::EPixelFormat format)
{
	AVPixelFormat avFormat{ AV_PIX_FMT_BGR0 };

	switch (format)
	{
		case Capture::EPixelFormat::BGRx: avFormat = AV_PIX_FMT_BGR0; break;
		case Capture::EPixelFormat::RGBx: avFormat = AV_PIX_FMT_RGB0; break;
		case Capture::EPixelFormat::BGRA: avFormat = AV_PIX_FMT_BGRA; break;
		case Capture::EPixelFormat::RGBA: avFormat = AV_PIX_FMT_RGBA; break;
	}

	return avFormat;
}

void FreeDrmDescriptor(void* pOpaque, uint8_t* pData)
{
	// The descriptor only; the descriptors' file descriptors belong to PipeWire.
	av_free(pData);
}

std::string Describe(int error)
{
	char text[AV_ERROR_MAX_STRING_SIZE]{};
	av_strerror(error, text, sizeof(text));

	return std::string{ text };
}
} // namespace

//////////////////////////////////////////////////////////////////////////
bool CEncoder::OpenDevice()
{
	// The same node the capability probe answered for: two cards in one machine rarely encode the same
	// set of codecs.
	std::string const& path{ GetCapabilities().devicePath };

	if (path.empty())
	{
		gLog.Error("No VAAPI render node could be opened. Klip encodes on the GPU.");
	}
	else
	{
		int const result{ av_hwdevice_ctx_create(&m_pDeviceRef, AV_HWDEVICE_TYPE_VAAPI, path.c_str(),
		                                         nullptr, 0) };

		if (result < 0)
		{
			gLog.Error("Could not open {}: {}", path, Describe(result));
			m_pDeviceRef = nullptr;
		}
		else
		{
			std::snprintf(m_devicePath, sizeof(m_devicePath), "%s", path.c_str());
		}
	}

	return m_pDeviceRef != nullptr;
}

//////////////////////////////////////////////////////////////////////////
bool CEncoder::OpenDrmDevice()
{
	int const result{ av_hwdevice_ctx_create(&m_pDrmDeviceRef, AV_HWDEVICE_TYPE_DRM, m_devicePath, nullptr,
	                                         0) };

	if (result < 0)
	{
		gLog.Error("Could not open {} as a DRM device: {}", m_devicePath, Describe(result));
		m_pDrmDeviceRef = nullptr;
	}

	return m_pDrmDeviceRef != nullptr;
}

//////////////////////////////////////////////////////////////////////////
bool CEncoder::OpenFilterGraph(SSettings const& settings)
{
	TGE_PROFILE_SCOPE_N("Encoder: filter graph");

	bool ready{ false };

	m_pDrmFramesRef = av_hwframe_ctx_alloc(m_pDrmDeviceRef);

	if (m_pDrmFramesRef == nullptr)
	{
		gLog.Error("Could not allocate the DRM frame pool.");
	}
	else
	{
		AVHWFramesContext* pFrames{ reinterpret_cast<AVHWFramesContext*>(m_pDrmFramesRef->data) };
		pFrames->format = AV_PIX_FMT_DRM_PRIME;
		pFrames->sw_format = ToAvFormat(settings.sourceFormat);
		pFrames->width = static_cast<int>(settings.width);
		pFrames->height = static_cast<int>(settings.height);

		// The compositor owns the buffers; nothing is allocated from this pool.
		pFrames->initial_pool_size = 0;

		int const framesResult{ av_hwframe_ctx_init(m_pDrmFramesRef) };

		if (framesResult < 0)
		{
			gLog.Error("Could not initialise the DRM frame pool: {}", Describe(framesResult));
		}
		else
		{
			m_pGraph = avfilter_graph_alloc();

			AVFilterContext* pHwmap{ nullptr };
			AVFilterContext* pCrop{ nullptr };
			AVFilterContext* pScale{ nullptr };

			bool const cropping{ m_region.width != settings.width || m_region.height != settings.height ||
				                 m_region.x != 0 || m_region.y != 0 };

			// Allocated and configured before init: a hardware pixel format is rejected while
			// hw_frames_ctx is still unset, which is what an args string would do.
			m_pGraphSource = avfilter_graph_alloc_filter(m_pGraph, avfilter_get_by_name("buffer"), "in");

			int result{ m_pGraphSource != nullptr ? 0 : AVERROR(ENOMEM) };

			if (result >= 0)
			{
				AVBufferSrcParameters* pParameters{ av_buffersrc_parameters_alloc() };
				pParameters->format = AV_PIX_FMT_DRM_PRIME;
				pParameters->width = static_cast<int>(settings.width);
				pParameters->height = static_cast<int>(settings.height);
				pParameters->time_base = AVRational{ 1, MicrosecondsPerSecond };
				pParameters->hw_frames_ctx = m_pDrmFramesRef;
				result = av_buffersrc_parameters_set(m_pGraphSource, pParameters);
				av_free(pParameters);
			}

			if (result >= 0)
			{
				result = avfilter_init_str(m_pGraphSource, nullptr);
			}

			if (result >= 0)
			{
				pHwmap = avfilter_graph_alloc_filter(m_pGraph, avfilter_get_by_name("hwmap"), "hwmap");
				result = pHwmap != nullptr ? 0 : AVERROR(ENOMEM);
			}

			if (result >= 0)
			{
				// The device Klip already opened, rather than derive_device making a second one from
				// the DRM node. Either way the surface never leaves the card.
				pHwmap->hw_device_ctx = av_buffer_ref(m_pDeviceRef);
				result = avfilter_init_str(pHwmap, nullptr);
			}

			if (result >= 0 && cropping)
			{
				char cropArguments[128];
				std::snprintf(cropArguments, sizeof(cropArguments), "w=%u:h=%u:x=%u:y=%u", m_region.width,
				              m_region.height, m_region.x, m_region.y);

				// crop only marks the rectangle; scale_vaapi reads it and does the work on the card.
				result = avfilter_graph_create_filter(&pCrop, avfilter_get_by_name("crop"), "crop",
				                                      cropArguments, nullptr, m_pGraph);
			}

			if (result >= 0)
			{
				// The size is not optional. crop only annotates a hardware frame, leaving the link at
				// its original size, so an unsized scale_vaapi blows the cropped region back up to fill
				// it.
				char scaleArguments[128];
				std::snprintf(scaleArguments, sizeof(scaleArguments), "w=%u:h=%u:format=nv12",
				              m_outputWidth, m_outputHeight);

				result = avfilter_graph_create_filter(&pScale, avfilter_get_by_name("scale_vaapi"),
				                                      "scale", scaleArguments, nullptr, m_pGraph);
			}

			if (result >= 0)
			{
				result = avfilter_graph_create_filter(&m_pGraphSink, avfilter_get_by_name("buffersink"),
				                                      "out", nullptr, nullptr, m_pGraph);
			}

			if (result >= 0)
			{
				result = avfilter_link(m_pGraphSource, 0, pHwmap, 0);
			}

			if (result >= 0)
			{
				result = cropping ? avfilter_link(pHwmap, 0, pCrop, 0) : avfilter_link(pHwmap, 0, pScale, 0);
			}

			if (result >= 0 && cropping)
			{
				result = avfilter_link(pCrop, 0, pScale, 0);
			}

			if (result >= 0)
			{
				result = avfilter_link(pScale, 0, m_pGraphSink, 0);
			}

			if (result >= 0)
			{
				result = avfilter_graph_config(m_pGraph, nullptr);
			}

			if (result < 0)
			{
				gLog.Error("Could not build the GPU conversion graph: {}", Describe(result));
			}
			else
			{
				AVBufferRef* const pSinkFrames{ av_buffersink_get_hw_frames_ctx(m_pGraphSink) };

				if (pSinkFrames == nullptr)
				{
					gLog.Error("The conversion graph produced no GPU frames context.");
				}
				else
				{
					m_pFramesRef = av_buffer_ref(pSinkFrames);
					ready = true;
				}
			}
		}
	}

	return ready;
}

//////////////////////////////////////////////////////////////////////////
bool CEncoder::OpenVaapiFrames(SSettings const& settings)
{
	bool ready{ false };

	m_pFramesRef = av_hwframe_ctx_alloc(m_pDeviceRef);

	if (m_pFramesRef == nullptr)
	{
		gLog.Error("Could not allocate the VAAPI frame pool.");
	}
	else
	{
		AVHWFramesContext* pFrames{ reinterpret_cast<AVHWFramesContext*>(m_pFramesRef->data) };
		pFrames->format = AV_PIX_FMT_VAAPI;
		pFrames->sw_format = AV_PIX_FMT_NV12;
		pFrames->width = static_cast<int>(m_outputWidth);
		pFrames->height = static_cast<int>(m_outputHeight);
		pFrames->initial_pool_size = 20;

		int const result{ av_hwframe_ctx_init(m_pFramesRef) };

		if (result < 0)
		{
			gLog.Error("Could not initialise the VAAPI frame pool: {}", Describe(result));
		}
		else
		{
			ready = true;
		}
	}

	return ready;
}

//////////////////////////////////////////////////////////////////////////
bool CEncoder::OpenContainer(SSettings const& settings)
{
	int const allocResult{ avformat_alloc_output_context2(&m_pFormatContext, nullptr, nullptr,
	                                                      settings.outputPath.c_str()) };

	bool const opened{ allocResult >= 0 && m_pFormatContext != nullptr };

	if (opened)
	{
		// Must be set before the header. A stalled audio track otherwise holds a full ten seconds of
		// video in memory before the interleaver gives up on it.
		m_pFormatContext->max_interleave_delta = AV_TIME_BASE;
	}

	if (!opened)
	{
		gLog.Error("Could not derive a container for {}: {}", settings.outputPath, Describe(allocResult));
	}

	return opened;
}

//////////////////////////////////////////////////////////////////////////
bool CEncoder::OpenEncoder(SSettings const& settings)
{
	TGE_PROFILE_SCOPE_N("Encoder: open codec");

	bool opened{ false };

	std::string const encoderName{ GetCodecEncoder(settings.codec) };
	AVCodec const*    pCodec{ avcodec_find_encoder_by_name(encoderName.c_str()) };

	if (pCodec == nullptr)
	{
		gLog.Error("This FFmpeg has no {} encoder.", encoderName);
	}
	else
	{
		m_pCodecContext = avcodec_alloc_context3(pCodec);

		if (m_pCodecContext == nullptr)
		{
			gLog.Error("Could not allocate the encoder context.");
		}
		else
		{
			m_pCodecContext->width = static_cast<int>(m_outputWidth);
			m_pCodecContext->height = static_cast<int>(m_outputHeight);
			m_pCodecContext->pix_fmt = AV_PIX_FMT_VAAPI;
			m_pCodecContext->time_base = AVRational{ 1, MicrosecondsPerSecond };

			// Rate control reads framerate, and falls back to 1/time_base -- a million frames a
			// second -- when it is unset, which starves every frame of bits.
			m_pCodecContext->framerate = AVRational{
				settings.maxFrameRate != 0 ? static_cast<int>(settings.maxFrameRate) : NominalFrameRate,
				1
			};

			m_pCodecContext->gop_size = GopSize;
			m_pCodecContext->max_b_frames = 0;
			m_pCodecContext->hw_frames_ctx = av_buffer_ref(m_pFramesRef);

			av_opt_set(m_pCodecContext->priv_data, "rc_mode", "CQP", 0);

			// av1_vaapi has no qp option at all; global_quality is the one knob all three read. Never
			// set AV_CODEC_FLAG_QSCALE with it -- that divides the value by FF_QP2LAMBDA.
			m_pCodecContext->global_quality = ResolveGlobalQuality(settings.codec, settings.quality);

			// Set before the codec opens or it never fills extradata, which the muxer needs.
			if ((m_pFormatContext->oformat->flags & AVFMT_GLOBALHEADER) != 0)
			{
				m_pCodecContext->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
			}

			int const openResult{ avcodec_open2(m_pCodecContext, pCodec, nullptr) };

			if (openResult < 0)
			{
				gLog.Error("Could not open {}: {}", encoderName, Describe(openResult));
			}
			else
			{
				// A driver without packed sequence headers leaves it empty, and the muxer cannot
				// reconstruct one for every codec.
				if ((m_pCodecContext->flags & AV_CODEC_FLAG_GLOBAL_HEADER) != 0 &&
				    m_pCodecContext->extradata_size == 0)
				{
					gLog.Warning("The encoder wrote no global header; {} may not be playable.",
					             settings.outputPath);
				}

				opened = true;
			}
		}
	}

	return opened;
}

//////////////////////////////////////////////////////////////////////////
// Never fails the recording: sound is worth losing to keep the picture, which is the opposite of what
// a failed avformat_write_header would do.
bool CEncoder::OpenAudioEncoder(SSettings const& settings)
{
	if (settings.audio.numSources > 0)
	{
		AVCodec const* pCodec{ avcodec_find_encoder(AV_CODEC_ID_AAC) };

		// An AAC stream in a container that cannot carry it fails the header, and that loses the video
		// too. WebM is the case that matters: its codecs are Opus and Vorbis.
		bool const carries{ avformat_query_codec(m_pFormatContext->oformat, AV_CODEC_ID_AAC,
		                                         FF_COMPLIANCE_NORMAL) == 1 };

		if (pCodec == nullptr)
		{
			gLog.Warning("This FFmpeg has no AAC encoder; recording without sound.");
		}
		else if (!carries)
		{
			gLog.Warning("{} cannot carry AAC; recording without sound.",
			             m_pFormatContext->oformat->name);
		}
		else
		{
			m_pAudioCodecContext = avcodec_alloc_context3(pCodec);

			if (m_pAudioCodecContext == nullptr)
			{
				gLog.Warning("Could not allocate the audio encoder; recording without sound.");
			}
			else
			{
				m_pAudioCodecContext->sample_fmt = AV_SAMPLE_FMT_FLTP;
				m_pAudioCodecContext->sample_rate = static_cast<int>(settings.audio.sampleRate);
				m_pAudioCodecContext->bit_rate = GetAudioBitsPerSecond(settings.audio.quality);
				m_pAudioCodecContext->time_base = AVRational{ 1,
					                                          static_cast<int>(
						                                          settings.audio.sampleRate) };
				av_channel_layout_default(&m_pAudioCodecContext->ch_layout,
				                          static_cast<int>(settings.audio.numChannels));

				if ((m_pFormatContext->oformat->flags & AVFMT_GLOBALHEADER) != 0)
				{
					m_pAudioCodecContext->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
				}

				int const openResult{ avcodec_open2(m_pAudioCodecContext, pCodec, nullptr) };

				if (openResult < 0)
				{
					gLog.Warning("Could not open the audio encoder ({}); recording without sound.",
					             Describe(openResult));
					avcodec_free_context(&m_pAudioCodecContext);
				}
				else
				{
					m_pAudioFrame = av_frame_alloc();
					m_pMixFrame = av_frame_alloc();

					for (AVFrame* const pFrame : { m_pAudioFrame, m_pMixFrame })
					{
						if (pFrame != nullptr)
						{
							pFrame->format = AV_SAMPLE_FMT_FLTP;
							pFrame->nb_samples = m_pAudioCodecContext->frame_size;
							av_channel_layout_copy(&pFrame->ch_layout,
							                       &m_pAudioCodecContext->ch_layout);
							av_frame_get_buffer(pFrame, 0);
						}
					}

					for (uint32_t index{ 0 }; index < settings.audio.numSources; ++index)
					{
						m_audioSources[index].pFifo = av_audio_fifo_alloc(
							AV_SAMPLE_FMT_FLTP, static_cast<int>(settings.audio.numChannels),
							m_pAudioCodecContext->frame_size * 8);
					}

					m_silence.assign(static_cast<size_t>(m_pAudioCodecContext->frame_size), 0.0f);

					m_audioGain = settings.audio.gain;
					m_numAudioSources = settings.audio.numSources;
					m_audioSampleRate = settings.audio.sampleRate;
					m_numAudioChannels = settings.audio.numChannels;
				}
			}
		}
	}

	return true;
}

//////////////////////////////////////////////////////////////////////////
bool CEncoder::OpenVideoStream(SSettings const& settings)
{
	bool opened{ false };

	m_pStream = avformat_new_stream(m_pFormatContext, nullptr);

	if (m_pStream == nullptr)
	{
		gLog.Error("Could not add a video stream to the container.");
	}
	else
	{
		m_pStream->time_base = m_pCodecContext->time_base;

		int const parameterResult{ avcodec_parameters_from_context(m_pStream->codecpar,
		                                                           m_pCodecContext) };

		if (parameterResult < 0)
		{
			gLog.Error("Could not copy the encoder parameters: {}", Describe(parameterResult));
		}
		else
		{
			// FFmpeg's tag table answers hev1 first, which Safari and QuickTime refuse to play.
			if (settings.codec == ECodec::Hevc &&
			    std::string_view{ m_pFormatContext->oformat->name } == "mp4")
			{
				m_pStream->codecpar->codec_tag = MKTAG('h', 'v', 'c', '1');
			}

			opened = true;
		}
	}

	return opened;
}

//////////////////////////////////////////////////////////////////////////
bool CEncoder::OpenAudioStream()
{
	bool opened{ true };

	if (m_pAudioCodecContext != nullptr)
	{
		m_pAudioStream = avformat_new_stream(m_pFormatContext, nullptr);

		if (m_pAudioStream == nullptr)
		{
			gLog.Error("Could not add an audio stream to the container.");
			opened = false;
		}
		else
		{
			m_pAudioStream->time_base = m_pAudioCodecContext->time_base;

			int const parameterResult{ avcodec_parameters_from_context(m_pAudioStream->codecpar,
			                                                           m_pAudioCodecContext) };

			if (parameterResult < 0)
			{
				gLog.Error("Could not copy the audio encoder parameters: {}", Describe(parameterResult));
				opened = false;
			}
		}
	}

	return opened;
}

//////////////////////////////////////////////////////////////////////////
bool CEncoder::OpenOutput(SSettings const& settings)
{
	TGE_PROFILE_SCOPE_N("Encoder: write header");

	bool opened{ false };

	int const ioResult{ avio_open(&m_pFormatContext->pb, settings.outputPath.c_str(), AVIO_FLAG_WRITE) };

	if (ioResult < 0)
	{
		gLog.Error("Could not open {} for writing: {}", settings.outputPath, Describe(ioResult));
	}
	else
	{
		int const headerResult{ avformat_write_header(m_pFormatContext, nullptr) };

		if (headerResult < 0)
		{
			gLog.Error("Could not write the container header: {}", Describe(headerResult));
		}
		else
		{
			m_headerWritten = true;
			opened = true;
		}
	}

	return opened;
}

//////////////////////////////////////////////////////////////////////////
bool CEncoder::Initialize(SSettings const& settings)
{
	TGE_PROFILE_SCOPE_N("Encoder: open");

	bool ready{ false };

	m_memory = settings.memory;
	m_region = ResolveRegion(settings);
	m_outputWidth = m_region.width;
	m_outputHeight = m_region.height;
	m_pVideoPacket = av_packet_alloc();
	m_pAudioPacket = av_packet_alloc();
	m_pDrmFrame = av_frame_alloc();
	m_pFilteredFrame = av_frame_alloc();
	m_pSoftwareFrame = av_frame_alloc();
	m_pHardwareFrame = av_frame_alloc();

	if (m_pVideoPacket == nullptr || m_pAudioPacket == nullptr || m_pDrmFrame == nullptr || m_pFilteredFrame == nullptr ||
	    m_pSoftwareFrame == nullptr || m_pHardwareFrame == nullptr)
	{
		gLog.Error("Could not allocate the encoder's frames.");
	}
	else
	{
		bool prepared{ false };

		if (settings.memory == Capture::EFrameMemory::DmaBuf)
		{
			prepared = OpenDevice() && OpenDrmDevice() && OpenFilterGraph(settings);
		}
		else
		{
			m_pSoftwareFrame->format = AV_PIX_FMT_NV12;
			m_pSoftwareFrame->width = static_cast<int>(m_outputWidth);
			m_pSoftwareFrame->height = static_cast<int>(m_outputHeight);

			int const bufferResult{ av_frame_get_buffer(m_pSoftwareFrame, 0) };

			if (bufferResult < 0)
			{
				gLog.Error("Could not allocate the staging frame: {}", Describe(bufferResult));
			}
			else
			{
				prepared = OpenDevice() && OpenVaapiFrames(settings);
			}
		}

		m_firstTimestampNs = settings.firstTimestampNs;
		m_hasFirstTimestamp = settings.firstTimestampNs != 0;

		if (prepared && OpenContainer(settings) && OpenEncoder(settings) &&
		    OpenAudioEncoder(settings) && OpenVideoStream(settings) && OpenAudioStream() &&
		    OpenOutput(settings))
		{
			if (m_outputWidth != settings.width || m_outputHeight != settings.height)
			{
				gLog.Info("Encoding {}x{} from {}x{} at +{},{} {} {} ({}) to {}", m_outputWidth,
				          m_outputHeight, settings.width, settings.height, m_region.x, m_region.y,
				          GetCodecEncoder(settings.codec), GetQualityName(settings.quality),
				          settings.memory == Capture::EFrameMemory::DmaBuf ? "zero-copy" : "cpu copy",
				          settings.outputPath);
			}
			else
			{
				gLog.Info("Encoding {}x{} {} {} ({}) to {}", m_outputWidth, m_outputHeight,
				          GetCodecEncoder(settings.codec), GetQualityName(settings.quality),
				          settings.memory == Capture::EFrameMemory::DmaBuf ? "zero-copy" : "cpu copy",
				          settings.outputPath);
			}
			ready = true;
		}
	}

	if (!ready)
	{
		Terminate();
	}

	return ready;
}

//////////////////////////////////////////////////////////////////////////
void CEncoder::Terminate()
{
	TGE_PROFILE_SCOPE_N("Stop: encoder teardown");

	if (m_pScaler != nullptr)
	{
		sws_freeContext(m_pScaler);
		m_pScaler = nullptr;
	}

	if (m_pGraph != nullptr)
	{
		avfilter_graph_free(&m_pGraph);
		m_pGraphSource = nullptr;
		m_pGraphSink = nullptr;
	}

	if (m_pFormatContext != nullptr)
	{
		if (m_pFormatContext->pb != nullptr)
		{
			avio_closep(&m_pFormatContext->pb);
		}

		avformat_free_context(m_pFormatContext);
		m_pFormatContext = nullptr;
	}

	m_pStream = nullptr;
	m_pAudioStream = nullptr;

	avcodec_free_context(&m_pCodecContext);
	avcodec_free_context(&m_pAudioCodecContext);
	av_buffer_unref(&m_pFramesRef);
	av_buffer_unref(&m_pDeviceRef);
	av_buffer_unref(&m_pDrmFramesRef);
	av_buffer_unref(&m_pDrmDeviceRef);
	av_frame_free(&m_pFilteredFrame);
	av_frame_free(&m_pDrmFrame);
	av_frame_free(&m_pHardwareFrame);
	av_frame_free(&m_pSoftwareFrame);
	av_packet_free(&m_pVideoPacket);
	av_packet_free(&m_pAudioPacket);
	av_frame_free(&m_pAudioFrame);
	av_frame_free(&m_pMixFrame);

	for (SAudioSource& audio : m_audioSources)
	{
		if (audio.pFifo != nullptr)
		{
			av_audio_fifo_free(audio.pFifo);
		}

		audio = SAudioSource{};
	}

	m_silence.clear();

	m_numAudioSources = 0;
	m_audioSampleRate = 0;
	m_numAudioChannels = 0;
	m_numAudioSamples = 0;
	m_lastAudioDriftNs = 0;

	m_headerWritten = false;
	m_hasFirstTimestamp = false;
	m_numFramesEncoded = 0;
	m_bytesWritten = 0;
	m_nextPts = 0;
	m_lastPts = 0;
	m_pLastEncoded = nullptr;
}

//////////////////////////////////////////////////////////////////////////
void CEncoder::FillSilence(uint32_t source, uint64_t numSamples)
{
	int const frameSize{ m_pAudioCodecContext->frame_size };

	std::array<void*, Capture::MaxAudioChannels> planes{};

	for (uint32_t channel{ 0 }; channel < m_numAudioChannels; ++channel)
	{
		planes[channel] = m_silence.data();
	}

	uint64_t written{ 0 };

	while (written < numSamples)
	{
		int const chunk{ static_cast<int>(
			std::min<uint64_t>(numSamples - written, static_cast<uint64_t>(frameSize))) };

		if (av_audio_fifo_write(m_audioSources[source].pFifo, planes.data(), chunk) != chunk)
		{
			written = numSamples;
		}
		else
		{
			written += static_cast<uint64_t>(chunk);
		}
	}
}

//////////////////////////////////////////////////////////////////////////
void CEncoder::LevelSources(int64_t toleranceNs)
{
	int const tolerance{ static_cast<int>(toleranceNs * m_audioSampleRate / 1000000000) };
	int       longest{ 0 };

	for (uint32_t index{ 0 }; index < m_numAudioSources; ++index)
	{
		longest = std::max(longest, av_audio_fifo_size(m_audioSources[index].pFifo));
	}

	TGE_PROFILE_PLOT("Audio backlog (samples)", static_cast<int64_t>(longest));

	for (uint32_t index{ 0 }; index < m_numAudioSources; ++index)
	{
		SAudioSource& audio{ m_audioSources[index] };

		int const behind{ longest - av_audio_fifo_size(audio.pFifo) };

		if (behind > tolerance)
		{
			FillSilence(index, static_cast<uint64_t>(behind));

			audio.numSamples += static_cast<uint64_t>(behind);
			audio.numSilent += static_cast<uint64_t>(behind);

			// Only the running mix can tell a stall from the straddle at Stop, where the streams are
			// terminated one after another and a few milliseconds apart is expected.
			if (toleranceNs > 0 && !audio.warnedStall)
			{
				gLog.Warning("Audio source {} stopped delivering; filling its track with silence.",
				             index);
				audio.warnedStall = true;
			}
		}
	}
}

//////////////////////////////////////////////////////////////////////////
void CEncoder::WriteSilence(uint32_t source, uint64_t nanoseconds)
{
	SAudioSource& audio{ m_audioSources[source] };

	uint64_t const numSamples{ nanoseconds * m_audioSampleRate / 1000000000ULL };

	FillSilence(source, numSamples);

	audio.numSamples += numSamples;
	audio.numSilent += numSamples;
	++audio.numGaps;
}

//////////////////////////////////////////////////////////////////////////
bool CEncoder::SubmitAudio(uint32_t source, Capture::SAudioBuffer const& buffer)
{
	TGE_PROFILE_SCOPE_N("Audio: submit");

	bool submitted{ false };

	if (m_pAudioCodecContext != nullptr && source < m_numAudioSources && m_hasFirstTimestamp &&
	    buffer.numFrames > 0)
	{
		// Deferred so the wait is timed apart from the work: a lock_guard folds the two together.
		std::unique_lock<std::mutex> guard{ m_audioMutex, std::defer_lock };

		{
			TGE_PROFILE_SCOPE_N("Wait: audio lock");
			guard.lock();
		}

		SAudioSource& audio{ m_audioSources[source] };

		uint64_t const durationNs{ static_cast<uint64_t>(buffer.numFrames) * 1000000000ULL /
			                       m_audioSampleRate };
		uint64_t const endNs{ buffer.timestampNs + durationNs };

		if (endNs <= m_firstTimestampNs)
		{
			submitted = true;
		}
		else
		{
			// Trim to the epoch rather than dropping the whole buffer: a dropped one shifts the whole
			// track up to a buffer early, and early is the direction that is heard.
			uint32_t skipped{ 0 };

			if (buffer.timestampNs < m_firstTimestampNs)
			{
				skipped = static_cast<uint32_t>((m_firstTimestampNs - buffer.timestampNs) *
				                                m_audioSampleRate / 1000000000ULL);
				skipped = std::min(skipped, buffer.numFrames);
			}

			uint64_t const startNs{ std::max(buffer.timestampNs, m_firstTimestampNs) };

			int64_t const driftNs{ static_cast<int64_t>(startNs - m_firstTimestampNs) -
				                   static_cast<int64_t>(audio.numSamples * 1000000000ULL /
				                                        m_audioSampleRate) };

			m_lastAudioDriftNs = driftNs;

			// A quantum change alone costs one buffer of apparent drift -- measured at 21 ms -- so the
			// threshold sits well above that and only real holes are filled.
			if (driftNs > GapThresholdNs)
			{
				WriteSilence(source, static_cast<uint64_t>(driftNs));

				if (audio.numGaps == 1)
				{
					gLog.Warning("Audio source {} skipped {} ms; filling with silence.", source,
					             driftNs / 1000000);
				}
			}

			std::array<void const*, Capture::MaxAudioChannels> planes{};

			for (uint32_t channel{ 0 }; channel < m_numAudioChannels; ++channel)
			{
				float const* const pPlane{ buffer.pPlanes[std::min(channel, buffer.numChannels - 1)] };
				planes[channel] = pPlane != nullptr ? pPlane + skipped : nullptr;
			}

			int const numFrames{ static_cast<int>(buffer.numFrames - skipped) };

			if (numFrames > 0 && planes[0] != nullptr)
			{
				av_audio_fifo_write(audio.pFifo, const_cast<void**>(planes.data()), numFrames);
				audio.numSamples += static_cast<uint64_t>(numFrames);
			}

			LevelSources(StallToleranceNs);

			submitted = EncodeAudioFrames();
		}
	}

	return submitted;
}

//////////////////////////////////////////////////////////////////////////
bool CEncoder::EncodeMixedFrame(int numSamples)
{
	TGE_PROFILE_SCOPE_N("Audio: mix");

	bool encoded{ true };

	// The frames keep a whole frame's capacity: av_frame_make_writable sizes a replacement from
	// nb_samples, so a short one shortens it no earlier than the send and puts it straight back.
	av_frame_make_writable(m_pAudioFrame);
	av_audio_fifo_read(m_audioSources[0].pFifo, reinterpret_cast<void**>(m_pAudioFrame->data),
	                   numSamples);

	// Gain lands here, on the way into the mix, so it is applied exactly once per sample and only ever
	// to what is written to the file.
	for (uint32_t channel{ 0 }; channel < m_numAudioChannels; ++channel)
	{
		float* const pSamples{ reinterpret_cast<float*>(m_pAudioFrame->data[channel]) };

		for (int sample{ 0 }; sample < numSamples; ++sample)
		{
			pSamples[sample] = std::clamp(pSamples[sample] * m_audioGain[0], -1.0f, 1.0f);
		}
	}

	for (uint32_t index{ 1 }; index < m_numAudioSources; ++index)
	{
		av_frame_make_writable(m_pMixFrame);
		av_audio_fifo_read(m_audioSources[index].pFifo, reinterpret_cast<void**>(m_pMixFrame->data),
		                   numSamples);

		for (uint32_t channel{ 0 }; channel < m_numAudioChannels; ++channel)
		{
			float* const       pMixed{ reinterpret_cast<float*>(m_pAudioFrame->data[channel]) };
			float const* const pAdded{ reinterpret_cast<float const*>(m_pMixFrame->data[channel]) };

			for (int sample{ 0 }; sample < numSamples; ++sample)
			{
				pMixed[sample] = std::clamp(pMixed[sample] + pAdded[sample] * m_audioGain[index],
				                            -1.0f, 1.0f);
			}
		}
	}

	m_pAudioFrame->nb_samples = numSamples;
	m_pAudioFrame->pts = static_cast<int64_t>(m_numAudioSamples);

	int const sendResult{ avcodec_send_frame(m_pAudioCodecContext, m_pAudioFrame) };

	m_pAudioFrame->nb_samples = m_pAudioCodecContext->frame_size;

	if (sendResult < 0)
	{
		gLog.Error("Could not encode an audio frame: {}", Describe(sendResult));
		encoded = false;
	}
	else
	{
		m_numAudioSamples += static_cast<uint64_t>(numSamples);
		encoded = DrainPackets(m_pAudioCodecContext, m_pAudioStream, m_pAudioPacket);
	}

	return encoded;
}

//////////////////////////////////////////////////////////////////////////
bool CEncoder::EncodeAudioFrames()
{
	bool encoded{ true };
	int const frameSize{ m_pAudioCodecContext->frame_size };

	bool ready{ true };

	while (ready && encoded)
	{
		for (uint32_t index{ 0 }; index < m_numAudioSources; ++index)
		{
			ready = ready && av_audio_fifo_size(m_audioSources[index].pFifo) >= frameSize;
		}

		if (ready)
		{
			encoded = EncodeMixedFrame(frameSize);
		}
	}

	return encoded;
}

//////////////////////////////////////////////////////////////////////////
void CEncoder::FinishAudio()
{
	TGE_PROFILE_SCOPE_N("Stop: audio tail");

	if (m_pAudioCodecContext != nullptr)
	{
		std::lock_guard<std::mutex> const guard{ m_audioMutex };

		// Levelling bounds the tail to one frame: the mix drains only what every source holds, so a
		// source that fell behind leaves the others an unbounded backlog.
		LevelSources(0);

		EncodeAudioFrames();

		// AAC accepts a short final frame, so the fifo tail is not thrown away.
		int const remaining{ av_audio_fifo_size(m_audioSources[0].pFifo) };

		if (remaining > 0)
		{
			EncodeMixedFrame(remaining);
		}

		avcodec_send_frame(m_pAudioCodecContext, nullptr);
		DrainPackets(m_pAudioCodecContext, m_pAudioStream, m_pAudioPacket);

		{
			std::lock_guard<std::mutex> const muxGuard{ m_muxMutex };
			av_interleaved_write_frame(m_pFormatContext, nullptr);
		}
	}
}

//////////////////////////////////////////////////////////////////////////
bool CEncoder::DrainPackets(AVCodecContext* pContext, AVStream const* pStream, AVPacket* pPacket)
{
	TGE_PROFILE_SCOPE_N("Mux: drain");

	bool drained{ true };
	int result{ 0 };

	while (result >= 0)
	{
		result = avcodec_receive_packet(pContext, pPacket);

		if (result == AVERROR(EAGAIN) || result == AVERROR_EOF)
		{
			break;
		}

		if (result < 0)
		{
			gLog.Error("Could not read an encoded packet: {}", Describe(result));
			drained = false;
		}
		else
		{
			av_packet_rescale_ts(pPacket, pContext->time_base, pStream->time_base);
			pPacket->stream_index = pStream->index;
			m_bytesWritten.fetch_add(static_cast<uint64_t>(pPacket->size), std::memory_order_relaxed);

			int writeResult{ 0 };

			{
				// Deferred for the same reason as the audio mutex: the wait is the number that matters.
				std::unique_lock<std::mutex> guard{ m_muxMutex, std::defer_lock };

				{
					TGE_PROFILE_SCOPE_N("Wait: mux lock");
					guard.lock();
				}

				TGE_PROFILE_SCOPE_N("Mux: write");
				writeResult = av_interleaved_write_frame(m_pFormatContext, pPacket);
			}

			av_packet_unref(pPacket);

			if (writeResult < 0)
			{
				gLog.Error("Could not write a packet: {}", Describe(writeResult));
				drained = false;
				result = writeResult;
			}
		}
	}

	return drained;
}

//////////////////////////////////////////////////////////////////////////
bool CEncoder::SendToEncoder(AVFrame* pFrame, Capture::SFrame const& frame)
{
	TGE_PROFILE_SCOPE_N("Encode: submit");

	int64_t pts{ m_nextPts };

	if (frame.timestampNs != 0 && m_hasFirstTimestamp)
	{
		pts = static_cast<int64_t>((frame.timestampNs - m_firstTimestampNs) / 1000);
	}

	// An encoder rejects a timestamp that does not advance.
	if (m_numFramesEncoded > 0 && pts <= m_nextPts - NominalFrameMicroseconds)
	{
		pts = m_nextPts;
	}

	m_nextPts = pts + NominalFrameMicroseconds;
	m_lastPts = pts;
	pFrame->pts = pts;

	bool submitted{ false };

	m_pLastEncoded = pFrame;

	int const sendResult{ avcodec_send_frame(m_pCodecContext, pFrame) };

	if (sendResult < 0)
	{
		gLog.Error("Could not submit a frame to the encoder: {}", Describe(sendResult));
	}
	else if (DrainPackets(m_pCodecContext, m_pStream, m_pVideoPacket))
	{
		++m_numFramesEncoded;
		submitted = true;
	}

	return submitted;
}

//////////////////////////////////////////////////////////////////////////
void CEncoder::WaitForSurface(AVFrame const* pFrame)
{
	TGE_PROFILE_SCOPE_N("Wait: GPU surface");

	if (pFrame != nullptr && pFrame->hw_frames_ctx != nullptr)
	{
		AVHWFramesContext const* pFrames{
			reinterpret_cast<AVHWFramesContext const*>(pFrame->hw_frames_ctx->data)
		};
		AVHWDeviceContext const* pDevice{
			reinterpret_cast<AVHWDeviceContext const*>(pFrames->device_ref->data)
		};
		AVVAAPIDeviceContext const* pVaapi{
			static_cast<AVVAAPIDeviceContext const*>(pDevice->hwctx)
		};

		VASurfaceID const surface{ static_cast<VASurfaceID>(
			reinterpret_cast<uintptr_t>(pFrame->data[3])) };

		VAStatus const status{ vaSyncSurface(pVaapi->display, surface) };

		if (status != VA_STATUS_SUCCESS && !m_reportedSyncFailure)
		{
			m_reportedSyncFailure = true;
			gLog.Error("Could not wait for the GPU: {}", vaErrorStr(status));
		}
	}
}

//////////////////////////////////////////////////////////////////////////
bool CEncoder::SubmitDmaBuf(Capture::SFrame const& frame)
{
	TGE_PROFILE_SCOPE_N("Encode: dmabuf");

	// Heap-allocated and wrapped in an AVBufferRef: buffersrc references the frame, and a hardware frame
	// that owns nothing cannot be referenced.
	AVDRMFrameDescriptor* const pDescriptor{ static_cast<AVDRMFrameDescriptor*>(
		av_mallocz(sizeof(AVDRMFrameDescriptor))) };

	if (pDescriptor == nullptr)
	{
		gLog.Error("Could not allocate a frame descriptor.");
		return false;
	}

	pDescriptor->nb_objects = 1;
	pDescriptor->objects[0].fd = frame.planes[0].fd;
	pDescriptor->objects[0].size = frame.planes[0].size;
	pDescriptor->objects[0].format_modifier = frame.modifier;

	pDescriptor->nb_layers = 1;
	pDescriptor->layers[0].format = ToDrmFourcc(frame.format);
	pDescriptor->layers[0].nb_planes = static_cast<int>(frame.numPlanes);

	for (uint32_t plane{ 0 }; plane < frame.numPlanes; ++plane)
	{
		pDescriptor->layers[0].planes[plane].object_index = 0;
		pDescriptor->layers[0].planes[plane].offset = frame.planes[plane].offset;
		pDescriptor->layers[0].planes[plane].pitch = frame.planes[plane].stride;
	}

	av_frame_unref(m_pDrmFrame);
	m_pDrmFrame->format = AV_PIX_FMT_DRM_PRIME;
	m_pDrmFrame->width = static_cast<int>(frame.width);
	m_pDrmFrame->height = static_cast<int>(frame.height);
	m_pDrmFrame->data[0] = reinterpret_cast<uint8_t*>(pDescriptor);
	m_pDrmFrame->buf[0] = av_buffer_create(reinterpret_cast<uint8_t*>(pDescriptor),
	                                       sizeof(AVDRMFrameDescriptor), FreeDrmDescriptor, nullptr, 0);
	m_pDrmFrame->hw_frames_ctx = av_buffer_ref(m_pDrmFramesRef);
	m_pDrmFrame->pts = static_cast<int64_t>(frame.timestampNs / 1000);

	bool submitted{ false };

	int result{ av_buffersrc_add_frame_flags(m_pGraphSource, m_pDrmFrame, AV_BUFFERSRC_FLAG_KEEP_REF) };

	if (result < 0)
	{
		gLog.Error("Could not push a frame into the conversion graph: {}", Describe(result));
	}
	else
	{
		av_frame_unref(m_pFilteredFrame);

		result = av_buffersink_get_frame(m_pGraphSink, m_pFilteredFrame);

		if (result < 0)
		{
			gLog.Error("The conversion graph produced no frame: {}", Describe(result));
		}
		else
		{
			// vaapi_vpp submits the conversion and returns; it never syncs. Without waiting, the caller
			// hands this buffer back to the compositor while the GPU is still reading it, and the next
			// frame is drawn over the one being converted.
			WaitForSurface(m_pFilteredFrame);

			submitted = SendToEncoder(m_pFilteredFrame, frame);
		}
	}

	av_frame_unref(m_pDrmFrame);

	return submitted;
}

//////////////////////////////////////////////////////////////////////////
bool CEncoder::SubmitMapped(Capture::SFrame const& frame)
{
	TGE_PROFILE_SCOPE_N("Encode: mapped");

	bool submitted{ false };

	if (m_pScaler == nullptr || frame.format != m_sourceFormat)
	{
		sws_freeContext(m_pScaler);
		m_sourceFormat = frame.format;
		m_pScaler = sws_getContext(static_cast<int>(m_region.width), static_cast<int>(m_region.height),
		                           ToAvFormat(frame.format), m_pCodecContext->width,
		                           m_pCodecContext->height, AV_PIX_FMT_NV12, SWS_BILINEAR, nullptr,
		                           nullptr, nullptr);
	}

	if (m_pScaler == nullptr)
	{
		gLog.Error("Could not build the pixel converter.");
	}
	else if (av_frame_make_writable(m_pSoftwareFrame) < 0)
	{
		gLog.Error("The staging frame could not be made writable.");
	}
	else
	{
		// Cropping here is just where the read starts; there is no separate pass.
		size_t const offset{ static_cast<size_t>(m_region.y) * frame.stride +
			                 static_cast<size_t>(m_region.x) * 4 };

		uint8_t const* sourcePlanes[4]{ frame.pPixels + offset, nullptr, nullptr, nullptr };
		int const sourceStrides[4]{ static_cast<int>(frame.stride), 0, 0, 0 };

		int converted{ 0 };

		{
			TGE_PROFILE_SCOPE_N("Encode: convert");
			converted = sws_scale(m_pScaler, sourcePlanes, sourceStrides, 0,
			                      static_cast<int>(m_region.height), m_pSoftwareFrame->data,
			                      m_pSoftwareFrame->linesize);
		}

		if (converted <= 0)
		{
			gLog.Error("The pixel conversion produced nothing.");
		}

		av_frame_unref(m_pHardwareFrame);

		int const poolResult{ av_hwframe_get_buffer(m_pFramesRef, m_pHardwareFrame, 0) };

		if (poolResult < 0)
		{
			gLog.Error("The VAAPI frame pool is exhausted: {}", Describe(poolResult));
		}
		else
		{
			int uploadResult{ 0 };

			{
				TGE_PROFILE_SCOPE_N("Encode: upload");
				uploadResult = av_hwframe_transfer_data(m_pHardwareFrame, m_pSoftwareFrame, 0);
			}

			if (uploadResult < 0)
			{
				gLog.Error("Could not upload a frame to the GPU: {}", Describe(uploadResult));
			}
			else
			{
				submitted = SendToEncoder(m_pHardwareFrame, frame);
			}
		}
	}

	return submitted;
}

//////////////////////////////////////////////////////////////////////////
bool CEncoder::SubmitFrame(Capture::SFrame const& frame)
{
	bool submitted{ false };

	if (m_pCodecContext == nullptr)
	{
		gLog.Error("SubmitFrame called before the encoder was ready.");
	}
	else if (frame.memory == Capture::EFrameMemory::DmaBuf)
	{
		submitted = SubmitDmaBuf(frame);
	}
	else if (frame.pPixels != nullptr)
	{
		submitted = SubmitMapped(frame);
	}

	return submitted;
}

//////////////////////////////////////////////////////////////////////////
bool CEncoder::Finish(uint64_t endTimestampNs)
{
	TGE_PROFILE_SCOPE_N("Stop: finish");

	bool finished{ false };

	if (m_pCodecContext == nullptr || !m_headerWritten)
	{
		gLog.Error("Finish called on an encoder that never opened.");
	}
	else
	{
		if (endTimestampNs != 0 && m_numFramesEncoded > 0 && m_hasFirstTimestamp &&
		    m_pLastEncoded != nullptr)
		{
			int64_t const endPts{ static_cast<int64_t>((endTimestampNs - m_firstTimestampNs) / 1000) };

			if (endPts > m_lastPts + NominalFrameMicroseconds)
			{
				m_pLastEncoded->pts = endPts;

				int const padResult{ avcodec_send_frame(m_pCodecContext, m_pLastEncoded) };

				if (padResult < 0)
				{
					gLog.Warning("Could not hold the last frame to the end: {}", Describe(padResult));
				}
				else if (!DrainPackets(m_pCodecContext, m_pStream, m_pVideoPacket))
				{
					gLog.Warning("The held last frame produced no packet.");
				}
				else
				{
					++m_numFramesEncoded;
				}
			}
		}

		int flushResult{ 0 };

		{
			TGE_PROFILE_SCOPE_N("Stop: flush encoder");
			flushResult = avcodec_send_frame(m_pCodecContext, nullptr);
		}

		if (flushResult < 0 && flushResult != AVERROR_EOF)
		{
			gLog.Error("Could not flush the encoder: {}", Describe(flushResult));
		}
		else if (DrainPackets(m_pCodecContext, m_pStream, m_pVideoPacket))
		{
			// Both encoders drained before the trailer, or the tail of one is lost.
			FinishAudio();

			int trailerResult{ 0 };

			{
				TGE_PROFILE_SCOPE_N("Stop: trailer");
				trailerResult = av_write_trailer(m_pFormatContext);
			}

			if (trailerResult < 0)
			{
				gLog.Error("Could not write the container trailer: {}", Describe(trailerResult));
			}
			else
			{
				if (m_pAudioCodecContext != nullptr)
				{
					double const seconds{ static_cast<double>(m_numAudioSamples) /
						                  static_cast<double>(m_audioSampleRate) };

					gLog.Info("Wrote {} frames and {:.2f} s of audio (drift {} ms)",
					          m_numFramesEncoded.load(), seconds, m_lastAudioDriftNs / 1000000);

					if (m_numAudioSamples == 0)
					{
						gLog.Error("Audio was asked for but not one sample reached the file.");
					}

					for (uint32_t index{ 0 }; index < m_numAudioSources; ++index)
					{
						if (m_audioSources[index].numGaps > 0)
						{
							gLog.Warning("Audio source {} needed {} gap(s) filled, {} ms of silence.",
							             index, m_audioSources[index].numGaps,
							             m_audioSources[index].numSilent * 1000 / m_audioSampleRate);
						}
					}
				}
				else
				{
					gLog.Info("Wrote {} frames", m_numFramesEncoded.load());
				}

				finished = true;
			}
		}
	}

	return finished;
}
} // namespace Klip::Encode
