#include "capture/pipewire_stream.hpp"

#include "log.hpp"

#include <libdrm/drm_fourcc.h>
#include <pipewire/pipewire.h>
#include <spa/buffer/meta.h>
#include <spa/param/video/format-utils.h>
#include <spa/pod/builder.h>
#include <tge/profiling/profiling.hpp>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <iterator>
#include <string>
#include <unistd.h>

namespace Klip::Capture
{
class CStreamImpl final
{
public:

	void HandleParamChanged(uint32_t id, spa_pod const* pParam);
	void HandleProcess();
	void Fixate(spa_pod_prop const* pProperty);
	void AnnounceBuffers();

	pw_thread_loop* pLoop{ nullptr };
	pw_context*     pContext{ nullptr };
	pw_core*        pCore{ nullptr };
	pw_stream*      pStream{ nullptr };
	spa_hook        streamListener{};

	CPipeWireStream::FrameCallback callback;
	CPipeWireStream::EndedCallback onEnded;

	std::atomic<uint32_t> width{ 0 };
	std::atomic<uint32_t> height{ 0 };
	std::atomic<uint64_t> numFrames{ 0 };

	// What the compositor actually agreed to, which is not what was asked for: uncapped settles at the
	// display's refresh rate, and that is the number the encoder has to be told.
	std::atomic<uint32_t> negotiatedMaxFrameRate{ 0 };

	EPixelFormat format{ EPixelFormat::BGRx };
	bool         hasFormat{ false };
	uint64_t     modifier{ DRM_FORMAT_MOD_INVALID };
	bool         useDmaBuf{ false };
	uint32_t     lastCropWidth{ 0 };
	uint32_t     lastCropHeight{ 0 };
	uint32_t     numCropChanges{ 0 };
	uint32_t     numBuffers{ 0 };
	uint32_t     maxFrameRate{ 0 };
	uint64_t     numSkipped{ 0 };
	bool         reportedQueueFailure{ false };
	bool         loggedMemory{ false };
	bool         wasStreaming{ false };
	bool         ended{ false };
	bool         terminating{ false };
};

namespace
{
constexpr uint32_t MaxDimension{ 8192 };
constexpr uint32_t MaxFrameRate{ 240 };

// INVALID lets the driver pick its own tiling, which is what makes an untouched compositor buffer
// importable. LINEAR is the fallback every driver can produce.
// The compositor allocates the minimum it can get away with unless asked, which leaves it redrawing into
// the buffer this client is still reading.
constexpr int32_t DefaultBufferCount{ 8 };
constexpr int32_t MinimumBufferCount{ 4 };
constexpr int32_t MaximumBufferCount{ 16 };

constexpr uint64_t PreferredModifiers[]{ DRM_FORMAT_MOD_INVALID, DRM_FORMAT_MOD_LINEAR };

spa_pod const* BuildFormat(spa_pod_builder& builder, uint64_t const* pModifiers, uint32_t numModifiers,
                           bool fixated, uint32_t maxFrameRate)
{
	uint32_t const ceiling{ maxFrameRate != 0 ? maxFrameRate : MaxFrameRate };

	spa_rectangle const defSize{ SPA_RECTANGLE(1920, 1080) };
	spa_rectangle const minSize{ SPA_RECTANGLE(1, 1) };
	spa_rectangle const maxSize{ SPA_RECTANGLE(MaxDimension, MaxDimension) };
	// The default has to sit inside the range: 60 offered against a ceiling of 30 is not a valid choice.
	spa_fraction const defRate{ SPA_FRACTION(ceiling, 1) };
	spa_fraction const minRate{ SPA_FRACTION(0, 1) };
	spa_fraction const maxRate{ SPA_FRACTION(ceiling, 1) };

	spa_pod_frame objectFrame{};
	spa_pod_builder_push_object(&builder, &objectFrame, SPA_TYPE_OBJECT_Format, SPA_PARAM_EnumFormat);

	spa_pod_builder_add(&builder, SPA_FORMAT_mediaType, SPA_POD_Id(SPA_MEDIA_TYPE_video), 0);
	spa_pod_builder_add(&builder, SPA_FORMAT_mediaSubtype, SPA_POD_Id(SPA_MEDIA_SUBTYPE_raw), 0);
	spa_pod_builder_add(&builder, SPA_FORMAT_VIDEO_format,
	                    SPA_POD_CHOICE_ENUM_Id(5, SPA_VIDEO_FORMAT_BGRx, SPA_VIDEO_FORMAT_BGRx,
	                                           SPA_VIDEO_FORMAT_RGBx, SPA_VIDEO_FORMAT_BGRA,
	                                           SPA_VIDEO_FORMAT_RGBA),
	                    0);

	if (numModifiers > 0)
	{
		// Without DONT_FIXATE the server has to choose blind; with it, it answers with the set it can
		// actually produce and expects this client to name one.
		uint32_t const flags{ fixated ? static_cast<uint32_t>(SPA_POD_PROP_FLAG_MANDATORY)
		                              : static_cast<uint32_t>(SPA_POD_PROP_FLAG_MANDATORY |
		                                                      SPA_POD_PROP_FLAG_DONT_FIXATE) };

		spa_pod_builder_prop(&builder, SPA_FORMAT_VIDEO_modifier, flags);

		spa_pod_frame choiceFrame{};
		spa_pod_builder_push_choice(&builder, &choiceFrame, SPA_CHOICE_Enum, 0);
		spa_pod_builder_long(&builder, static_cast<int64_t>(pModifiers[0]));

		for (uint32_t index{ 0 }; index < numModifiers; ++index)
		{
			spa_pod_builder_long(&builder, static_cast<int64_t>(pModifiers[index]));
		}

		spa_pod_builder_pop(&builder, &choiceFrame);
	}

	spa_pod_builder_add(&builder, SPA_FORMAT_VIDEO_size,
	                    SPA_POD_CHOICE_RANGE_Rectangle(&defSize, &minSize, &maxSize), 0);
	spa_pod_builder_add(&builder, SPA_FORMAT_VIDEO_framerate,
	                    SPA_POD_CHOICE_RANGE_Fraction(&defRate, &minRate, &maxRate), 0);

	// A screen cast is a variable rate source, so SPA_FORMAT_VIDEO_framerate settles at 0/1 and carries
	// no ceiling; the compositor states its own through maxFramerate, and that is the one to constrain.
	spa_pod_builder_add(&builder, SPA_FORMAT_VIDEO_maxFramerate,
	                    SPA_POD_CHOICE_RANGE_Fraction(&defRate, &minRate, &maxRate), 0);

	return static_cast<spa_pod const*>(spa_pod_builder_pop(&builder, &objectFrame));
}

bool ToPixelFormat(uint32_t spaFormat, EPixelFormat& format)
{
	bool known{ true };

	switch (spaFormat)
	{
		case SPA_VIDEO_FORMAT_BGRx: format = EPixelFormat::BGRx; break;
		case SPA_VIDEO_FORMAT_RGBx: format = EPixelFormat::RGBx; break;
		case SPA_VIDEO_FORMAT_BGRA: format = EPixelFormat::BGRA; break;
		case SPA_VIDEO_FORMAT_RGBA: format = EPixelFormat::RGBA; break;
		default: known = false; break;
	}

	return known;
}

char const* ToName(EPixelFormat format)
{
	char const* pName{ "BGRx" };

	switch (format)
	{
		case EPixelFormat::BGRx: pName = "BGRx"; break;
		case EPixelFormat::RGBx: pName = "RGBx"; break;
		case EPixelFormat::BGRA: pName = "BGRA"; break;
		case EPixelFormat::RGBA: pName = "RGBA"; break;
	}

	return pName;
}

void OnStreamStateChanged(void* pData, pw_stream_state, pw_stream_state state, char const* pError)
{
	CStreamImpl& impl{ *static_cast<CStreamImpl*>(pData) };

	if (state == PW_STREAM_STATE_ERROR)
	{
		gLog.Error("PipeWire stream failed: {}", pError != nullptr ? pError : "unknown");
	}
	else if (state == PW_STREAM_STATE_STREAMING)
	{
		impl.wasStreaming = true;
	}

	bool const finished{ state == PW_STREAM_STATE_UNCONNECTED || state == PW_STREAM_STATE_ERROR };

	if (finished && impl.wasStreaming && !impl.ended && !impl.terminating)
	{
		impl.ended = true;

		gLog.Warning("The compositor withdrew the screen cast.");

		if (impl.onEnded)
		{
			impl.onEnded();
		}
	}
}

void OnStreamParamChanged(void* pData, uint32_t id, spa_pod const* pParam)
{
	static_cast<CStreamImpl*>(pData)->HandleParamChanged(id, pParam);
}

void OnStreamProcess(void* pData)
{
	static_cast<CStreamImpl*>(pData)->HandleProcess();
}

void OnStreamAddBuffer(void* pData, pw_buffer*)
{
	CStreamImpl& impl{ *static_cast<CStreamImpl*>(pData) };
	++impl.numBuffers;
}

// Every member spelled out: -Wmissing-field-initializers rejects a partial designated initializer.
constexpr pw_stream_events StreamEvents{
	.version = PW_VERSION_STREAM_EVENTS,
	.destroy = nullptr,
	.state_changed = OnStreamStateChanged,
	.control_info = nullptr,
	.io_changed = nullptr,
	.param_changed = OnStreamParamChanged,
	.add_buffer = OnStreamAddBuffer,
	.remove_buffer = nullptr,
	.process = OnStreamProcess,
	.drained = nullptr,
	.command = nullptr,
	.trigger_done = nullptr,
};
} // namespace

//////////////////////////////////////////////////////////////////////////
void CStreamImpl::Fixate(spa_pod_prop const* pProperty)
{
	uint32_t numValues{ 0 };
	uint32_t choiceType{ 0 };
	spa_pod* pValues{ spa_pod_get_values(&pProperty->value, &numValues, &choiceType) };

	if (pValues == nullptr || numValues < 2)
	{
		gLog.Warning("The server offered no modifier to settle on; falling back to memory buffers.");
		useDmaBuf = false;
		AnnounceBuffers();
	}
	else
	{
		int64_t const* pOffered{ static_cast<int64_t const*>(SPA_POD_BODY_CONST(pValues)) };

		// Index 0 repeats the default; the offers follow it.
		uint64_t const chosen{ static_cast<uint64_t>(pOffered[1]) };

		uint8_t podBuffer[2048];
		spa_pod_builder builder = SPA_POD_BUILDER_INIT(podBuffer, sizeof(podBuffer));

		spa_pod const* params[2];
		params[0] = BuildFormat(builder, &chosen, 1, true, maxFrameRate);
		params[1] = BuildFormat(builder, nullptr, 0, false, maxFrameRate);

		if (pw_stream_update_params(pStream, params, 2) < 0)
		{
			gLog.Error("The server would not accept a settled modifier.");
		}
	}
}

//////////////////////////////////////////////////////////////////////////
void CStreamImpl::AnnounceBuffers()
{
	uint8_t podBuffer[1024];
	spa_pod_builder builder = SPA_POD_BUILDER_INIT(podBuffer, sizeof(podBuffer));

	uint32_t const dataType{ useDmaBuf ? (1u << SPA_DATA_DmaBuf)
	                                   : ((1u << SPA_DATA_MemFd) | (1u << SPA_DATA_MemPtr)) };

	spa_pod const* params[3];
	params[0] = static_cast<spa_pod const*>(
		spa_pod_builder_add_object(&builder, SPA_TYPE_OBJECT_ParamBuffers, SPA_PARAM_Buffers,
		                           SPA_PARAM_BUFFERS_buffers,
		                           SPA_POD_CHOICE_RANGE_Int(DefaultBufferCount, MinimumBufferCount,
		                                                    MaximumBufferCount),
		                           SPA_PARAM_BUFFERS_dataType, SPA_POD_CHOICE_FLAGS_Int(dataType)));
	params[1] = static_cast<spa_pod const*>(
		spa_pod_builder_add_object(&builder, SPA_TYPE_OBJECT_ParamMeta, SPA_PARAM_Meta,
		                           SPA_PARAM_META_type, SPA_POD_Id(SPA_META_Header),
		                           SPA_PARAM_META_size, SPA_POD_Int(sizeof(spa_meta_header))));
	params[2] = static_cast<spa_pod const*>(
		spa_pod_builder_add_object(&builder, SPA_TYPE_OBJECT_ParamMeta, SPA_PARAM_Meta,
		                           SPA_PARAM_META_type, SPA_POD_Id(SPA_META_VideoCrop),
		                           SPA_PARAM_META_size, SPA_POD_Int(sizeof(spa_meta_region))));

	if (pw_stream_update_params(pStream, params, 3) < 0)
	{
		gLog.Error("The server would not accept Klip's buffer requirements.");
	}
}

//////////////////////////////////////////////////////////////////////////
void CStreamImpl::HandleParamChanged(uint32_t id, spa_pod const* pParam)
{
	if (pParam != nullptr && id == SPA_PARAM_Format)
	{
		spa_video_info info{};

		if (spa_format_parse(pParam, &info.media_type, &info.media_subtype) < 0 ||
		    info.media_type != SPA_MEDIA_TYPE_video || info.media_subtype != SPA_MEDIA_SUBTYPE_raw)
		{
			gLog.Error("The stream negotiated a format Klip cannot read.");
		}
		else if (spa_format_video_raw_parse(pParam, &info.info.raw) < 0)
		{
			gLog.Error("Could not parse the negotiated video format.");
		}
		else if (!ToPixelFormat(info.info.raw.format, format))
		{
			gLog.Error("The stream negotiated an unsupported pixel layout.");
		}
		else
		{
			spa_pod_prop const* pModifier{ spa_pod_find_prop(pParam, nullptr,
			                                                 SPA_FORMAT_VIDEO_modifier) };

			if (pModifier != nullptr && (pModifier->flags & SPA_POD_PROP_FLAG_DONT_FIXATE) != 0)
			{
				Fixate(pModifier);
			}
			else
			{
				if (pModifier != nullptr)
				{
					modifier = static_cast<uint64_t>(
						*static_cast<int64_t const*>(SPA_POD_BODY_CONST(&pModifier->value)));
					useDmaBuf = true;
				}

				width.store(info.info.raw.size.width, std::memory_order_relaxed);
				height.store(info.info.raw.size.height, std::memory_order_relaxed);
				hasFormat = true;

				double const maxFps{ info.info.raw.max_framerate.denom != 0
					                     ? static_cast<double>(info.info.raw.max_framerate.num) /
					                           static_cast<double>(info.info.raw.max_framerate.denom)
					                     : 0.0 };

				// Nearest, not truncated: 60000/1001 is 59.94 and belongs at 60.
				negotiatedMaxFrameRate.store(static_cast<uint32_t>(std::lround(maxFps)),
				                             std::memory_order_relaxed);

				gLog.Info("Stream negotiated {}x{} {} at up to {:.2f} fps (asked for {})",
				          info.info.raw.size.width, info.info.raw.size.height, ToName(format), maxFps,
				          maxFrameRate != 0 ? std::to_string(maxFrameRate) : std::string{ "no cap" });

				AnnounceBuffers();
			}
		}
	}
}

//////////////////////////////////////////////////////////////////////////
void CStreamImpl::HandleProcess()
{
	TGE_PROFILE_SCOPE_N("Capture: frame");

	pw_buffer* pBuffer{ pw_stream_dequeue_buffer(pStream) };

	if (pBuffer != nullptr)
	{
		spa_buffer const* pSpaBuffer{ pBuffer->buffer };

		if (!loggedMemory && pSpaBuffer->n_datas > 0)
		{
			loggedMemory = true;

			char const* pKind{ "unknown" };

			switch (pSpaBuffer->datas[0].type)
			{
				case SPA_DATA_MemPtr: pKind = "MemPtr"; break;
				case SPA_DATA_MemFd: pKind = "MemFd"; break;
				case SPA_DATA_DmaBuf: pKind = "DmaBuf"; break;
				default: break;
			}

			if (pSpaBuffer->datas[0].type == SPA_DATA_DmaBuf)
			{
				gLog.Info("{} buffers arrive as {}, {} plane(s), modifier 0x{:x}", numBuffers, pKind,
				          pSpaBuffer->n_datas, modifier);
			}
			else
			{
				gLog.Info("{} buffers arrive as {}, {} plane(s)", numBuffers, pKind, pSpaBuffer->n_datas);
			}
		}

		SFrame frame;
		frame.width = width.load(std::memory_order_relaxed);
		frame.height = height.load(std::memory_order_relaxed);
		frame.format = format;

		// A dequeued buffer does not always carry a new picture. PipeWire says so through the chunk, and
		// encoding one that does not re-encodes whatever that buffer held the last time round.
		bool const carriesPicture{ pSpaBuffer->n_datas > 0 && pSpaBuffer->datas[0].chunk->size > 0 &&
			                       (pSpaBuffer->datas[0].chunk->flags & SPA_CHUNK_FLAG_CORRUPTED) == 0 };

		bool usable{ false };

		if (hasFormat && carriesPicture && pSpaBuffer->datas[0].type == SPA_DATA_DmaBuf)
		{
			frame.memory = EFrameMemory::DmaBuf;
			frame.modifier = modifier;
			frame.numPlanes = std::min(pSpaBuffer->n_datas, MaxPlanes);

			for (uint32_t plane{ 0 }; plane < frame.numPlanes; ++plane)
			{
				frame.planes[plane].fd = static_cast<int>(pSpaBuffer->datas[plane].fd);
				frame.planes[plane].offset = pSpaBuffer->datas[plane].chunk->offset;
				frame.planes[plane].stride = static_cast<uint32_t>(pSpaBuffer->datas[plane].chunk->stride);
				frame.planes[plane].size = pSpaBuffer->datas[plane].maxsize;
			}

			frame.stride = frame.planes[0].stride;
			usable = frame.planes[0].fd >= 0;
		}
		else if (hasFormat && carriesPicture && pSpaBuffer->datas[0].data != nullptr)
		{
			frame.memory = EFrameMemory::Mapped;
			frame.pPixels = static_cast<uint8_t const*>(pSpaBuffer->datas[0].data);
			frame.stride = static_cast<uint32_t>(pSpaBuffer->datas[0].chunk->stride);
			usable = true;
		}

		if (usable)
		{
			spa_meta_region const* pCrop{ static_cast<spa_meta_region const*>(
				spa_buffer_find_meta_data(pSpaBuffer, SPA_META_VideoCrop, sizeof(spa_meta_region))) };

			if (pCrop != nullptr && spa_meta_region_is_valid(pCrop))
			{
				frame.cropX = static_cast<uint32_t>(pCrop->region.position.x);
				frame.cropY = static_cast<uint32_t>(pCrop->region.position.y);
				frame.cropWidth = pCrop->region.size.width;
				frame.cropHeight = pCrop->region.size.height;

				if (frame.cropWidth != lastCropWidth || frame.cropHeight != lastCropHeight)
				{
					lastCropWidth = frame.cropWidth;
					lastCropHeight = frame.cropHeight;
					++numCropChanges;

					if (numCropChanges <= 5)
					{
						gLog.Info("Picture occupies {}x{} at +{},{} of the buffer (change {})",
						          frame.cropWidth, frame.cropHeight, frame.cropX, frame.cropY,
						          numCropChanges);
					}
				}
			}

			spa_meta_header const* pHeader{ static_cast<spa_meta_header const*>(
				spa_buffer_find_meta_data(pSpaBuffer, SPA_META_Header, sizeof(spa_meta_header))) };

			if (pHeader != nullptr && pHeader->pts > 0)
			{
				frame.timestampNs = static_cast<uint64_t>(pHeader->pts);
			}

			numFrames.fetch_add(1, std::memory_order_relaxed);

			if (callback)
			{
				callback(frame);
			}
		}

		if (!usable)
		{
			++numSkipped;
		}

		if (pw_stream_queue_buffer(pStream, pBuffer) < 0 && !reportedQueueFailure)
		{
			reportedQueueFailure = true;
			gLog.Error("A buffer could not be returned to the stream; capture will stall.");
		}

		TGE_PROFILE_FRAME();
	}
}

//////////////////////////////////////////////////////////////////////////
bool CPipeWireStream::Initialize(int pipeWireFd, uint32_t nodeId, uint32_t maxFrameRate,
                                 FrameCallback callback, EndedCallback onEnded)
{
	TGE_PROFILE_SCOPE_N("Capture: open stream");

	m_pImpl = new CStreamImpl();
	m_pImpl->callback = std::move(callback);
	m_pImpl->onEnded = std::move(onEnded);
	m_pImpl->maxFrameRate = maxFrameRate;

	m_pImpl->pLoop = pw_thread_loop_new("klip-capture", nullptr);

	bool started{ false };

	if (m_pImpl->pLoop == nullptr)
	{
		gLog.Error("Could not create the PipeWire loop.");
		::close(pipeWireFd);
	}
	else if (pw_thread_loop_start(m_pImpl->pLoop) < 0)
	{
		gLog.Error("Could not start the PipeWire loop.");
		::close(pipeWireFd);
	}
	else
	{
		pw_thread_loop_lock(m_pImpl->pLoop);

		m_pImpl->pContext = pw_context_new(pw_thread_loop_get_loop(m_pImpl->pLoop), nullptr, 0);

		if (m_pImpl->pContext == nullptr)
		{
			gLog.Error("Could not create the PipeWire context.");
			::close(pipeWireFd);
		}
		else
		{
			// pw_context_connect_fd owns the descriptor from here, including on failure.
			m_pImpl->pCore = pw_context_connect_fd(m_pImpl->pContext, pipeWireFd, nullptr, 0);

			if (m_pImpl->pCore == nullptr)
			{
				gLog.Error("Could not connect to the PipeWire remote.");
			}
			else
			{
				pw_properties* pProperties{ pw_properties_new(PW_KEY_MEDIA_TYPE, "Video",
				                                              PW_KEY_MEDIA_CATEGORY, "Capture",
				                                              PW_KEY_MEDIA_ROLE, "Screen", nullptr) };

				m_pImpl->pStream = pw_stream_new(m_pImpl->pCore, "klip", pProperties);

				if (m_pImpl->pStream == nullptr)
				{
					gLog.Error("Could not create the PipeWire stream.");
				}
				else
				{
					pw_stream_add_listener(m_pImpl->pStream, &m_pImpl->streamListener, &StreamEvents,
					                       m_pImpl);

					uint8_t podBuffer[2048];
					spa_pod_builder builder = SPA_POD_BUILDER_INIT(podBuffer, sizeof(podBuffer));

					// The modifier form first: PipeWire takes the first it can satisfy, so an unmodified
					// second entry keeps the memory path available when DMA-BUF is not on offer.
					spa_pod const* params[2];
					params[0] = BuildFormat(builder, PreferredModifiers,
					                        static_cast<uint32_t>(std::size(PreferredModifiers)), false,
					                        maxFrameRate);
					params[1] = BuildFormat(builder, nullptr, 0, false, maxFrameRate);

					int const connected{ pw_stream_connect(
						m_pImpl->pStream, PW_DIRECTION_INPUT, nodeId,
						static_cast<pw_stream_flags>(PW_STREAM_FLAG_AUTOCONNECT |
						                             PW_STREAM_FLAG_MAP_BUFFERS),
						params, 2) };

					if (connected < 0)
					{
						gLog.Error("Could not connect the PipeWire stream to node {}.", nodeId);
					}
					else
					{
						started = true;
					}
				}
			}
		}

		pw_thread_loop_unlock(m_pImpl->pLoop);
	}

	if (!started)
	{
		Terminate();
	}

	return started;
}

//////////////////////////////////////////////////////////////////////////
void CPipeWireStream::Terminate()
{
	TGE_PROFILE_SCOPE_N("Stop: capture stream");

	if (m_pImpl != nullptr)
	{
		if (m_pImpl->pLoop != nullptr)
		{
			// Locked for one store: a callback reading it stale reports our own teardown as a withdrawal.
			pw_thread_loop_lock(m_pImpl->pLoop);
			m_pImpl->terminating = true;
			pw_thread_loop_unlock(m_pImpl->pLoop);

			pw_thread_loop_stop(m_pImpl->pLoop);
		}

		if (m_pImpl->pStream != nullptr)
		{
			pw_stream_destroy(m_pImpl->pStream);
		}

		if (m_pImpl->pCore != nullptr)
		{
			pw_core_disconnect(m_pImpl->pCore);
		}

		if (m_pImpl->pContext != nullptr)
		{
			pw_context_destroy(m_pImpl->pContext);
		}

		if (m_pImpl->pLoop != nullptr)
		{
			pw_thread_loop_destroy(m_pImpl->pLoop);
		}

		m_numFrames = m_pImpl->numFrames.load(std::memory_order_relaxed);

		if (m_pImpl->numSkipped > 0)
		{
			gLog.Warning("Skipped {} buffer(s) that carried no new picture", m_pImpl->numSkipped);
		}

		delete m_pImpl;
		m_pImpl = nullptr;
	}
}

//////////////////////////////////////////////////////////////////////////
uint32_t CPipeWireStream::GetWidth() const
{
	return m_pImpl != nullptr ? m_pImpl->width.load(std::memory_order_relaxed) : 0;
}

//////////////////////////////////////////////////////////////////////////
uint32_t CPipeWireStream::GetHeight() const
{
	return m_pImpl != nullptr ? m_pImpl->height.load(std::memory_order_relaxed) : 0;
}

//////////////////////////////////////////////////////////////////////////
uint32_t CPipeWireStream::GetMaxFrameRate() const
{
	return m_pImpl != nullptr ? m_pImpl->negotiatedMaxFrameRate.load(std::memory_order_relaxed) : 0;
}

//////////////////////////////////////////////////////////////////////////
uint64_t CPipeWireStream::GetNumFrames() const
{
	return m_pImpl != nullptr ? m_pImpl->numFrames.load(std::memory_order_relaxed) : m_numFrames;
}
} // namespace Klip::Capture
