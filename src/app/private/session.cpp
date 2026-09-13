#include "session.hpp"

#include "log.hpp"

#include <tge/profiling/profiling.hpp>
#include <tge/threading/job_system.hpp>
#include <tge/threading/thread_name.hpp>

#include <algorithm>
#include <cstring>
#include <ctime>
#include <unistd.h>
#include <utility>

namespace Klip
{
namespace
{
constexpr uint32_t NumSlots{ 4 };

uint64_t MonotonicNs()
{
	timespec now{};
	clock_gettime(CLOCK_MONOTONIC, &now);

	return static_cast<uint64_t>(now.tv_sec) * 1000000000ULL + static_cast<uint64_t>(now.tv_nsec);
}

} // namespace

//////////////////////////////////////////////////////////////////////////
bool CSession::Initialize()
{
	m_portal.SetClosedCallback([this]() {
		if (m_onEnded)
		{
			m_onEnded();
		}
	});

	return m_portal.Initialize();
}

//////////////////////////////////////////////////////////////////////////
void CSession::Terminate()
{
	TGE_PROFILE_SCOPE_N("Exit: session");

	m_systemAudio.Terminate();
	m_microphoneAudio.Terminate();
	m_stream.Terminate();
	m_encoder.Terminate();
	m_portal.Terminate();
	m_slots.clear();
}

//////////////////////////////////////////////////////////////////////////
bool CSession::StartEncoder(Capture::SFrame const& frame)
{
	TGE_PROFILE_SCOPE_N("Encoder: start");

	Encode::SSettings settings;
	settings.outputPath = m_request.outputPath;
	settings.width = frame.width;
	settings.height = frame.height;
	settings.region = m_request.region;

	// A window arrives padded into a larger buffer; the stream says which part is real.
	if (settings.region.width == 0 && frame.cropWidth != 0)
	{
		settings.region = Encode::SRegion{ frame.cropX, frame.cropY, frame.cropWidth, frame.cropHeight };
	}
	settings.codec = m_request.codec;
	settings.quality = m_request.quality;
	// What was negotiated, not what was asked: uncapped settles at the refresh rate, and rate control
	// reasoning about 60 while being handed 75 misallocates every frame.
	uint32_t const negotiated{ m_stream.GetMaxFrameRate() };
	settings.maxFrameRate = negotiated != 0 ? negotiated : m_request.maxFrameRate;
	settings.audio.numSources = m_numAudioSources;
	settings.audio.quality = m_request.audioQuality;

	if (m_systemSource != InvalidSource)
	{
		settings.audio.gain[m_systemSource] = m_request.systemGain;
	}

	if (m_microphoneSource != InvalidSource)
	{
		settings.audio.gain[m_microphoneSource] = m_request.microphoneGain;
	}

	// A frame with no header timestamp still needs an epoch to place the audio against.
	settings.firstTimestampNs = frame.timestampNs != 0 ? frame.timestampNs : MonotonicNs();

	settings.sourceFormat = frame.format;
	settings.memory = frame.memory;
	settings.modifier = frame.modifier;

	bool const started{ m_encoder.Initialize(settings) };

	// A DMA-BUF frame is the compositor's own memory, valid only until the buffer goes back. There is
	// nothing to hand to another thread, so the ring and its copy exist for the mapped path alone.
	if (started && frame.memory == Capture::EFrameMemory::Mapped)
	{
		size_t const slotBytes{ static_cast<size_t>(frame.stride) * static_cast<size_t>(frame.height) };

		m_slots.resize(NumSlots);

		for (uint32_t index{ 0 }; index < NumSlots; ++index)
		{
			m_slots[index].pixels.resize(slotBytes);
			m_free.Enqueue(index);
		}

		gLog.Info("Frame ring: {} slots of {} KiB", NumSlots, slotBytes / 1024);

		m_encoderThread = std::thread(&CSession::EncoderMain, this);
	}

	return started;
}

//////////////////////////////////////////////////////////////////////////
// Every ticked source must open, or the recording does not start: a file that silently lacks the
// microphone it was asked for is discovered far too late.
bool CSession::OpenAudio()
{
	bool opened{ true };

	m_numAudioSources = 0;
	m_systemSource = InvalidSource;
	m_microphoneSource = InvalidSource;

	if (!m_request.systemAudio.nodeName.empty())
	{
		uint32_t const source{ m_numAudioSources };

		if (m_systemAudio.Initialize(
				m_request.systemAudio,
				[this, source](Capture::SAudioBuffer const& buffer) { OnAudio(source, buffer); },
				[]() { gLog.Error("The system audio source stopped; the rest is silent."); }))
		{
			m_systemSource = source;
			++m_numAudioSources;
		}
		else
		{
			m_startFailure = m_request.systemAudio.description;
			opened = false;
		}
	}

	if (opened && !m_request.microphone.nodeName.empty())
	{
		uint32_t const source{ m_numAudioSources };

		if (m_microphoneAudio.Initialize(
				m_request.microphone,
				[this, source](Capture::SAudioBuffer const& buffer) { OnAudio(source, buffer); },
				[]() { gLog.Error("The microphone stopped; the rest is silent."); }))
		{
			m_microphoneSource = source;
			++m_numAudioSources;
		}
		else
		{
			m_startFailure = m_request.microphone.description;
			opened = false;
		}
	}

	if (!opened)
	{
		m_systemAudio.Terminate();
		m_microphoneAudio.Terminate();
		m_numAudioSources = 0;
	}

	return opened;
}

//////////////////////////////////////////////////////////////////////////
void CSession::OnAudio(uint32_t source, Capture::SAudioBuffer const& buffer)
{
	// Before the first video frame there is no epoch to place these against.
	if (m_encoding.load(std::memory_order_acquire))
	{
		m_encoder.SubmitAudio(source, buffer);
	}
}

//////////////////////////////////////////////////////////////////////////
void CSession::NoteEncodeFailure()
{
	// The encoder has already said why. Once here, then a count, so a failing frame every frame does not
	// bury it.
	if (m_numEncodeFailures.fetch_add(1, std::memory_order_relaxed) == 0)
	{
		gLog.Error("A frame could not be encoded; the recording will be missing it.");
	}
}

//////////////////////////////////////////////////////////////////////////
void CSession::OnFrame(Capture::SFrame const& frame)
{
	if (!m_encoding.load(std::memory_order_acquire) && !m_startFailed.load(std::memory_order_acquire))
	{
		if (StartEncoder(frame))
		{
			m_encoding.store(true, std::memory_order_release);
		}
		else
		{
			gLog.Error("The encoder could not be started; this recording will produce nothing.");

			// It will not succeed on the next frame either, and retrying floods the log.
			m_startFailed.store(true, std::memory_order_release);
		}
	}

	if (m_encoding.load(std::memory_order_acquire))
	{
		m_lastFrameTimestampNs.store(frame.timestampNs, std::memory_order_relaxed);
		m_lastFrameArrivalNs.store(MonotonicNs(), std::memory_order_relaxed);

		if (frame.memory == Capture::EFrameMemory::DmaBuf)
		{
			if (!m_encoder.SubmitFrame(frame))
			{
				NoteEncodeFailure();
			}
		}
		else
		{
			uint32_t index{ 0 };

			if (m_free.Dequeue(index))
			{
				SSlot& slot{ m_slots[index] };
				slot.stride = frame.stride;
				slot.width = frame.width;
				slot.height = frame.height;
				slot.timestampNs = frame.timestampNs;
				slot.format = frame.format;

				size_t const bytes{ static_cast<size_t>(frame.stride) *
					                static_cast<size_t>(frame.height) };

				{
					TGE_PROFILE_SCOPE_N("Capture: copy to ring");
					std::memcpy(slot.pixels.data(), frame.pPixels,
					            std::min(bytes, slot.pixels.size()));
				}

				m_filled.Enqueue(index);
				m_filledCount.release();
			}
			else
			{
				// Dropping beats blocking: this runs on the PipeWire thread, which must return the
				// buffer.
				m_numDropped.fetch_add(1, std::memory_order_relaxed);

				// The load, not the increment: a plot argument is not evaluated without a profiler.
				TGE_PROFILE_PLOT("Frames dropped",
				                 static_cast<int64_t>(m_numDropped.load(std::memory_order_relaxed)));
			}
		}
	}
}

//////////////////////////////////////////////////////////////////////////
void CSession::EncoderMain()
{
	Tge::Threading::InitializeThread();
	Tge::Threading::SetCurrentThreadName("klip-encode");

	bool running{ true };

	while (running)
	{
		{
			TGE_PROFILE_SCOPE_N("Wait: ring slot");
			m_filledCount.acquire();
		}

		uint32_t index{ 0 };

		if (m_filled.Dequeue(index))
		{
			SSlot const& slot{ m_slots[index] };

			Capture::SFrame frame;
			frame.pPixels = slot.pixels.data();
			frame.stride = slot.stride;
			frame.width = slot.width;
			frame.height = slot.height;
			frame.timestampNs = slot.timestampNs;
			frame.format = slot.format;

			if (!m_encoder.SubmitFrame(frame))
			{
				NoteEncodeFailure();
			}

			m_free.Enqueue(index);
		}
		else if (m_stopping.load(std::memory_order_acquire))
		{
			running = false;
		}
	}

	Tge::Threading::FinalizeThread();
}

//////////////////////////////////////////////////////////////////////////
void CSession::Start(SRecordingRequest const& request, StartedCallback callback)
{
	m_request = request;
	m_stopping.store(false, std::memory_order_release);
	m_startFailed.store(false, std::memory_order_release);
	m_numDropped.store(0, std::memory_order_relaxed);
	m_numEncodeFailures.store(0, std::memory_order_relaxed);
	m_startFailure.clear();

	m_portal.Start(m_request.source, m_request.rememberWindow,
	               [this, callback = std::move(callback)](Capture::EPortalResult result,
	                                                      Capture::SStreamInfo const& info,
	                                                      int pipeWireFd) {
		bool started{ false };

		if (result == Capture::EPortalResult::Success)
		{
			if (OpenAudio())
			{
				// Initialize owns the descriptor from here, including on failure.
				started = m_stream.Initialize(
					pipeWireFd, info.nodeId, m_request.maxFrameRate,
					[this](Capture::SFrame const& frame) { OnFrame(frame); },
					[this]() {
						if (m_onEnded)
						{
							m_onEnded();
						}
					});
			}
			else
			{
				::close(pipeWireFd);
			}

			if (!started)
			{
				m_systemAudio.Terminate();
				m_microphoneAudio.Terminate();
				m_numAudioSources = 0;
			}
		}

		callback(started);
	});
}

//////////////////////////////////////////////////////////////////////////
bool CSession::Stop()
{
	TGE_PROFILE_SCOPE_N("Stop: session");

	// The producers first, so nothing lands in a ring -- or an encoder -- about to be torn down.
	m_systemAudio.Terminate();
	m_microphoneAudio.Terminate();
	m_stream.Terminate();

	m_portal.Close();

	if (m_encoderThread.joinable())
	{
		m_stopping.store(true, std::memory_order_release);
		m_filledCount.release();
		m_encoderThread.join();
	}

	bool finished{ false };

	if (m_encoding.load(std::memory_order_acquire))
	{
		// Only the elapsed part of CLOCK_MONOTONIC is used, so PipeWire's clock need not be the same one.
		uint64_t const lastFrame{ m_lastFrameTimestampNs.load(std::memory_order_relaxed) };
		uint64_t const arrival{ m_lastFrameArrivalNs.load(std::memory_order_relaxed) };
		uint64_t const endTimestampNs{ lastFrame != 0 ? lastFrame + (MonotonicNs() - arrival) : 0 };

		finished = m_encoder.Finish(endTimestampNs);
		m_lastStats = GetStats();
		m_encoder.Terminate();
		m_encoding.store(false, std::memory_order_release);
	}

	uint32_t index{ 0 };

	while (m_filled.Dequeue(index) || m_free.Dequeue(index))
	{
	}

	m_slots.clear();

	return finished;
}

//////////////////////////////////////////////////////////////////////////
SSessionStats CSession::GetStats() const
{
	SSessionStats stats;

	if (m_encoding.load(std::memory_order_acquire))
	{
		stats.numCaptured = m_stream.GetNumFrames();
		stats.numEncoded = m_encoder.GetNumFramesEncoded();
		stats.numDropped = m_numDropped.load(std::memory_order_relaxed);
		stats.bytesWritten = m_encoder.GetBytesWritten();
	}
	else
	{
		stats = m_lastStats;
	}

	return stats;
}
} // namespace Klip
