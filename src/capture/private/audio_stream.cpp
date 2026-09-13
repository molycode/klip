#include "capture/audio_stream.hpp"

#include "audio_loop.hpp"
#include "log.hpp"

#include <pipewire/pipewire.h>
#include <spa/param/audio/format-utils.h>
#include <spa/pod/builder.h>
#include <tge/profiling/profiling.hpp>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <limits>
#include <utility>

namespace Klip::Capture
{
namespace
{
constexpr uint32_t SampleRate{ 48000 };

constexpr int64_t FormatTimeoutNs{ 300000000 };
} // namespace

class CAudioImpl final
{
public:

	void HandleParamChanged(uint32_t id, spa_pod const* pParam);
	void HandleProcess();

	pw_thread_loop* pLoop{ nullptr };
	pw_stream*      pStream{ nullptr };
	spa_hook        streamListener{};

	CAudioStream::BufferCallback callback;
	CAudioStream::EndedCallback  onEnded;

	std::atomic<uint32_t> numChannels{ 0 };
	std::atomic<uint64_t> numBuffers{ 0 };
	std::atomic<float>    peaks[MaxAudioChannels]{};

	bool hasFormat{ false };
	bool failed{ false };
	bool wasStreaming{ false };
	bool ended{ false };
};

namespace
{
void OnStreamStateChanged(void* pData, pw_stream_state, pw_stream_state state, char const* pError)
{
	CAudioImpl& impl{ *static_cast<CAudioImpl*>(pData) };

	if (state == PW_STREAM_STATE_STREAMING)
	{
		impl.wasStreaming = true;
	}

	if (state == PW_STREAM_STATE_ERROR)
	{
		gLog.Error("The audio stream failed: {}", pError != nullptr ? pError : "no reason given");

		impl.failed = true;
		pw_thread_loop_signal(impl.pLoop, false);
	}

	bool const finished{ state == PW_STREAM_STATE_UNCONNECTED || state == PW_STREAM_STATE_ERROR };

	if (finished && impl.wasStreaming && !impl.ended)
	{
		impl.ended = true;

		gLog.Warning("The audio device stopped delivering.");

		if (impl.onEnded)
		{
			impl.onEnded();
		}
	}
}

void OnStreamParamChanged(void* pData, uint32_t id, spa_pod const* pParam)
{
	static_cast<CAudioImpl*>(pData)->HandleParamChanged(id, pParam);
}

void OnStreamProcess(void* pData)
{
	static_cast<CAudioImpl*>(pData)->HandleProcess();
}

// Every member spelled out: -Wmissing-field-initializers rejects a partial designated initializer.
constexpr pw_stream_events StreamEvents{
	.version = PW_VERSION_STREAM_EVENTS,
	.destroy = nullptr,
	.state_changed = OnStreamStateChanged,
	.control_info = nullptr,
	.io_changed = nullptr,
	.param_changed = OnStreamParamChanged,
	.add_buffer = nullptr,
	.remove_buffer = nullptr,
	.process = OnStreamProcess,
	.drained = nullptr,
	.command = nullptr,
	.trigger_done = nullptr,
};
} // namespace

//////////////////////////////////////////////////////////////////////////
void CAudioImpl::HandleParamChanged(uint32_t id, spa_pod const* pParam)
{
	if (pParam != nullptr && id == SPA_PARAM_Format)
	{
		spa_audio_info info{};

		if (spa_format_parse(pParam, &info.media_type, &info.media_subtype) >= 0 &&
		    info.media_type == SPA_MEDIA_TYPE_audio && info.media_subtype == SPA_MEDIA_SUBTYPE_raw &&
		    spa_format_audio_raw_parse(pParam, &info.info.raw) >= 0)
		{
			numChannels.store(info.info.raw.channels, std::memory_order_relaxed);
			hasFormat = true;

			gLog.Info("Audio capture at {} Hz, {} channel(s)", info.info.raw.rate,
			          info.info.raw.channels);

			pw_thread_loop_signal(pLoop, false);
		}
	}
}

//////////////////////////////////////////////////////////////////////////
void CAudioImpl::HandleProcess()
{
	TGE_PROFILE_SCOPE_N("Audio: buffer");

	pw_buffer* pBuffer{ pw_stream_dequeue_buffer(pStream) };

	if (pBuffer != nullptr)
	{
		spa_buffer const* pSpaBuffer{ pBuffer->buffer };
		uint32_t const    channels{ std::min(pSpaBuffer->n_datas, MaxAudioChannels) };

		SAudioBuffer buffer{};
		buffer.numChannels = channels;

		uint32_t numFrames{ std::numeric_limits<uint32_t>::max() };

		for (uint32_t channel{ 0 }; channel < channels; ++channel)
		{
			spa_data const& plane{ pSpaBuffer->datas[channel] };

			if (plane.data != nullptr && plane.chunk != nullptr)
			{
				uint32_t const offset{ std::min(plane.chunk->offset, plane.maxsize) };
				uint32_t const size{ std::min(plane.chunk->size, plane.maxsize - offset) };

				buffer.pPlanes[channel] = SPA_PTROFF(plane.data, offset, float const);
				numFrames = std::min(numFrames, size / static_cast<uint32_t>(sizeof(float)));
			}
		}

		buffer.numFrames = channels > 0 && numFrames != std::numeric_limits<uint32_t>::max()
		                       ? numFrames
		                       : 0;

		if (buffer.numFrames > 0 && buffer.pPlanes[0] != nullptr)
		{
			pw_time timing{};
			pw_stream_get_time_n(pStream, &timing, sizeof(timing));
			buffer.timestampNs = static_cast<uint64_t>(timing.now);

			for (uint32_t channel{ 0 }; channel < channels; ++channel)
			{
				float peak{ 0.0f };

				if (buffer.pPlanes[channel] != nullptr)
				{
					for (uint32_t frame{ 0 }; frame < buffer.numFrames; ++frame)
					{
						peak = std::max(peak, std::fabs(buffer.pPlanes[channel][frame]));
					}
				}

				peaks[channel].store(peak, std::memory_order_relaxed);
			}

			numBuffers.fetch_add(1, std::memory_order_relaxed);

			if (callback)
			{
				callback(buffer);
			}
		}

		pw_stream_queue_buffer(pStream, pBuffer);
	}
}

//////////////////////////////////////////////////////////////////////////
bool CAudioStream::Initialize(SAudioDevice const& device, BufferCallback callback, EndedCallback onEnded)
{
	TGE_PROFILE_SCOPE_N("Audio: open stream");

	m_pImpl = new CAudioImpl();
	m_pImpl->callback = std::move(callback);
	m_pImpl->onEnded = std::move(onEnded);
	m_pImpl->pLoop = gAudioLoop.Open();

	bool started{ false };

	if (m_pImpl->pLoop != nullptr)
	{
		pw_thread_loop_lock(m_pImpl->pLoop);

		pw_properties* pProperties{ pw_properties_new(
			PW_KEY_MEDIA_TYPE, "Audio", PW_KEY_MEDIA_CATEGORY, "Capture", PW_KEY_MEDIA_ROLE, "Production",
			PW_KEY_TARGET_OBJECT, device.nodeName.c_str(), nullptr) };

		if (device.isMonitor)
		{
			pw_properties_set(pProperties, PW_KEY_STREAM_CAPTURE_SINK, "true");
		}

		m_pImpl->pStream = pw_stream_new(gAudioLoop.GetCore(), "klip", pProperties);

		if (m_pImpl->pStream == nullptr)
		{
			gLog.Error("Could not create the audio stream.");
		}
		else
		{
			pw_stream_add_listener(m_pImpl->pStream, &m_pImpl->streamListener, &StreamEvents, m_pImpl);

			uint8_t         podBuffer[1024];
			spa_pod_builder builder = SPA_POD_BUILDER_INIT(podBuffer, sizeof(podBuffer));

			// Planar float is PipeWire's own and the only format the AAC encoder takes, so nothing
			// converts anywhere. Pinned rather than negotiated because the muxer needs the format
			// settled before it writes a header.
			spa_audio_info_raw rawInfo{};
			rawInfo.format = SPA_AUDIO_FORMAT_F32P;
			rawInfo.rate = SampleRate;
			rawInfo.channels = MaxAudioChannels;
			rawInfo.position[0] = SPA_AUDIO_CHANNEL_FL;
			rawInfo.position[1] = SPA_AUDIO_CHANNEL_FR;

			spa_pod const* params[1];
			params[0] = spa_format_audio_raw_build(&builder, SPA_PARAM_EnumFormat, &rawInfo);

			// Without DONT_RECONNECT the target is only a hint: a node that is gone falls back to the
			// default device, so a stream opens and quietly carries the wrong one. Measured -- a bogus
			// name linked to the default sink's monitor.
			int const connected{ pw_stream_connect(
				m_pImpl->pStream, PW_DIRECTION_INPUT, PW_ID_ANY,
				static_cast<pw_stream_flags>(PW_STREAM_FLAG_AUTOCONNECT | PW_STREAM_FLAG_MAP_BUFFERS |
			                                 PW_STREAM_FLAG_DONT_RECONNECT),
				params, 1) };

			if (connected < 0)
			{
				gLog.Error("Could not connect the audio stream to {}.", device.nodeName);
			}
			else
			{
				timespec deadline{};
				pw_thread_loop_get_time(m_pImpl->pLoop, &deadline, FormatTimeoutNs);

				int waited{ 0 };

				// Re-tested every wake because the loop is shared: another stream's signal lands here too.
				while (!m_pImpl->hasFormat && !m_pImpl->failed && waited == 0)
				{
					waited = pw_thread_loop_timed_wait_full(m_pImpl->pLoop, &deadline);
				}

				started = m_pImpl->hasFormat;

				if (!started && !m_pImpl->failed)
				{
					gLog.Error("The audio device {} never answered with a format.", device.nodeName);
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
void CAudioStream::Terminate()
{
	TGE_PROFILE_SCOPE_N("Stop: audio stream");

	if (m_pImpl != nullptr)
	{
		if (m_pImpl->pStream != nullptr)
		{
			// The loop outlives the stream, so the lock is what keeps the audio thread off it.
			pw_thread_loop_lock(m_pImpl->pLoop);
			spa_hook_remove(&m_pImpl->streamListener);
			pw_stream_destroy(m_pImpl->pStream);
			pw_thread_loop_unlock(m_pImpl->pLoop);
		}

		m_numBuffers = m_pImpl->numBuffers.load(std::memory_order_relaxed);

		delete m_pImpl;
		m_pImpl = nullptr;
	}
}

//////////////////////////////////////////////////////////////////////////
uint32_t CAudioStream::GetNumChannels() const
{
	return m_pImpl != nullptr ? m_pImpl->numChannels.load(std::memory_order_relaxed) : 0;
}

//////////////////////////////////////////////////////////////////////////
uint64_t CAudioStream::GetNumBuffers() const
{
	return m_pImpl != nullptr ? m_pImpl->numBuffers.load(std::memory_order_relaxed) : m_numBuffers;
}

//////////////////////////////////////////////////////////////////////////
float CAudioStream::GetChannelPeak(uint32_t channel) const
{
	float peak{ 0.0f };

	if (m_pImpl != nullptr && channel < MaxAudioChannels)
	{
		peak = m_pImpl->peaks[channel].load(std::memory_order_relaxed);
	}

	return peak;
}
} // namespace Klip::Capture
