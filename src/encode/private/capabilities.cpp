#include "encode/capabilities.hpp"

#include "encode/quality.hpp"
#include "log.hpp"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/hwcontext.h>
#include <libavutil/opt.h>
}

#include <tge/profiling/profiling.hpp>
#include <algorithm>
#include <array>
#include <cstdint>
#include <format>
#include <string>
#include <unistd.h>

namespace Klip::Encode
{
namespace
{
constexpr uint32_t FirstRenderNode{ 128 };
constexpr uint32_t LastRenderNode{ 136 };

// Cards refuse a surface below their own minimum -- 128 on Navi 32 -- and AV1 aligns to 64.
constexpr int ProbeSize{ 256 };
constexpr int ProbeFrameRate{ 60 };

struct SNodeProbe final
{
	std::array<bool, CodecCount> encodes{};
	bool                         opened{ false };
};

SCapabilities gCapabilities;

//////////////////////////////////////////////////////////////////////////
AVBufferRef* MakeProbeFrames(AVBufferRef* pDeviceRef)
{
	AVBufferRef* pFramesRef{ av_hwframe_ctx_alloc(pDeviceRef) };

	if (pFramesRef != nullptr)
	{
		AVHWFramesContext* pFrames{ reinterpret_cast<AVHWFramesContext*>(pFramesRef->data) };
		pFrames->format = AV_PIX_FMT_VAAPI;
		pFrames->sw_format = AV_PIX_FMT_NV12;
		pFrames->width = ProbeSize;
		pFrames->height = ProbeSize;
		pFrames->initial_pool_size = 1;

		if (av_hwframe_ctx_init(pFramesRef) < 0)
		{
			av_buffer_unref(&pFramesRef);
		}
	}

	return pFramesRef;
}

//////////////////////////////////////////////////////////////////////////
// Asking libva for the profile list is not enough: it reports decode-only profiles the encoder cannot
// use, and av1_vaapi additionally refuses a driver without VAConfigAttribEncAV1Ext2. Opening it answers
// the whole question.
bool CanEncode(ECodec codec, AVBufferRef* pFramesRef)
{
	TGE_PROFILE_SCOPE_N("Probe: encoder open");

	bool encodes{ false };

	std::string const name{ GetCodecEncoder(codec) };
	AVCodec const*    pCodec{ avcodec_find_encoder_by_name(name.c_str()) };

	if (pCodec != nullptr)
	{
		AVCodecContext* pContext{ avcodec_alloc_context3(pCodec) };

		if (pContext != nullptr)
		{
			pContext->width = ProbeSize;
			pContext->height = ProbeSize;
			pContext->pix_fmt = AV_PIX_FMT_VAAPI;
			pContext->time_base = AVRational{ 1, ProbeFrameRate };
			pContext->framerate = AVRational{ ProbeFrameRate, 1 };
			pContext->max_b_frames = 0;
			pContext->global_quality = ResolveGlobalQuality(codec, EQuality::Balanced);
			pContext->hw_frames_ctx = av_buffer_ref(pFramesRef);

			// The probe must fail the same way a recording would, so it asks for the same rate control.
			av_opt_set(pContext->priv_data, "rc_mode", "CQP", 0);

			// A card that cannot encode this is an expected answer here, not something to report.
			pContext->log_level_offset = AV_LOG_FATAL;

			encodes = avcodec_open2(pContext, pCodec, nullptr) >= 0;

			avcodec_free_context(&pContext);
		}
	}

	return encodes;
}

//////////////////////////////////////////////////////////////////////////
std::string DescribeCodecs(std::array<bool, CodecCount> const& encodes)
{
	std::string names;

	for (size_t index{ 0 }; index < CodecCount; ++index)
	{
		if (encodes[index])
		{
			names += names.empty() ? "" : " ";
			names += GetCodecName(static_cast<ECodec>(index));
		}
	}

	return names;
}

//////////////////////////////////////////////////////////////////////////
SNodeProbe ProbeNode(std::string const& path)
{
	TGE_PROFILE_SCOPE_N("Probe: node");

	SNodeProbe   probe{};
	AVBufferRef* pDeviceRef{ nullptr };

	if (av_hwdevice_ctx_create(&pDeviceRef, AV_HWDEVICE_TYPE_VAAPI, path.c_str(), nullptr, 0) >= 0)
	{
		probe.opened = true;

		AVBufferRef* pFramesRef{ MakeProbeFrames(pDeviceRef) };

		if (pFramesRef == nullptr)
		{
			gLog.Warning("Could not make a probe frame pool on {}; its codecs go unasked.", path);
		}
		else
		{
			for (size_t index{ 0 }; index < CodecCount; ++index)
			{
				probe.encodes[index] = CanEncode(static_cast<ECodec>(index), pFramesRef);
			}

			av_buffer_unref(&pFramesRef);
		}

		av_buffer_unref(&pDeviceRef);
	}

	return probe;
}
} // namespace

//////////////////////////////////////////////////////////////////////////
void InitializeCapabilities()
{
	TGE_PROFILE_SCOPE_N("Probe: all nodes");

	size_t bestCodecs{ 0 };

	for (uint32_t node{ FirstRenderNode }; node < LastRenderNode; ++node)
	{
		std::string const candidate{ std::format("/dev/dri/renderD{}", node) };

		if (::access(candidate.c_str(), R_OK | W_OK) == 0)
		{
			SNodeProbe const probe{ ProbeNode(candidate) };
			size_t const     numCodecs{ static_cast<size_t>(std::ranges::count(probe.encodes, true)) };

			// A node encoding none of them is still a device the encoder can try, so the first is kept.
			bool const isBetter{ numCodecs > bestCodecs ||
			                     (probe.opened && gCapabilities.devicePath.empty()) };

			if (isBetter)
			{
				bestCodecs = numCodecs;
				gCapabilities.devicePath = candidate;
				gCapabilities.encodes = probe.encodes;
			}
		}
	}

	std::string const encodable{ DescribeCodecs(gCapabilities.encodes) };

	if (gCapabilities.devicePath.empty())
	{
		gLog.Error("No VAAPI render node could be opened. Klip encodes on the GPU.");
	}
	else if (encodable.empty())
	{
		gLog.Warning("{} encodes none of Klip's codecs; offering them all anyway.",
		             gCapabilities.devicePath);
	}
	else
	{
		gLog.Info("VAAPI device {} encodes {}", gCapabilities.devicePath, encodable);
	}
}

//////////////////////////////////////////////////////////////////////////
SCapabilities const& GetCapabilities()
{
	return gCapabilities;
}

//////////////////////////////////////////////////////////////////////////
bool IsCodecOffered(ECodec codec)
{
	bool anyEncodes{ false };

	for (bool encodes : gCapabilities.encodes)
	{
		anyEncodes = anyEncodes || encodes;
	}

	return !anyEncodes || gCapabilities.encodes[static_cast<size_t>(codec)];
}
} // namespace Klip::Encode
