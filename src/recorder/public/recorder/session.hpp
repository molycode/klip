#pragma once

#include "capture/audio_stream.hpp"
#include "capture/pipewire_stream.hpp"
#include "capture/portal_session.hpp"
#include "encode/encoder.hpp"

#include <tge/non_copyable.hpp>
#include <tge/threading/mpsc_queue.hpp>

#include <atomic>
#include <limits>
#include <cstdint>
#include <functional>
#include <semaphore>
#include <string>
#include <thread>
#include <vector>

namespace Klip::Recorder
{
struct SRecordingRequest final
{
	std::string          outputPath;
	Encode::SRegion      region;
	Capture::ESourceType source{ Capture::ESourceType::Screen };
	bool                 rememberWindow{ false };

	// The portal's grant from last time, if it was kept; a picker appears without one.
	std::string          restoreToken;
	Encode::ECodec       codec{ Encode::ECodec::H264 };
	Encode::EQuality     quality{ Encode::EQuality::Balanced };

	// Never a floor: an idle screen sends less than it.
	uint32_t             maxFrameRate{ 0 };

	Capture::SAudioDevice systemAudio;
	Capture::SAudioDevice microphone;

	float systemGain{ 1.0f };
	float microphoneGain{ 1.0f };

	Encode::EQuality audioQuality{ Encode::EQuality::High };
};

struct SSessionStats final
{
	uint64_t numCaptured{ 0 };
	uint64_t numEncoded{ 0 };
	uint64_t numDropped{ 0 };
	uint64_t bytesWritten{ 0 };
};

class CSession final : private Tge::SNoCopyNoMove
{
public:

	static constexpr uint32_t InvalidSource{ std::numeric_limits<uint32_t>::max() };

	// Asynchronous because the compositor decides whether it starts, not Klip. Fires from Update.
	using StartedCallback = std::function<void(bool)>;

	// Fires on the PipeWire thread or the bus thread.
	using EndedCallback = std::function<void()>;

	// Fires on the bus thread, and asks only that Update be called on the thread that owns the session.
	using WakeCallback = std::function<void()>;

	CSession() = default;
	~CSession() = default;

	bool Initialize(WakeCallback wake);
	void Terminate();

	void SetEndedCallback(EndedCallback callback) { m_onEnded = std::move(callback); }

	void Start(SRecordingRequest const& request, StartedCallback callback);
	bool Stop();

	// Carries on a Start the portal has answered.
	void Update();

	bool IsRecording() const { return m_encoding.load(std::memory_order_acquire); }

	SSessionStats GetStats() const;

	std::string const& GetStartFailure() const { return m_startFailure; }

	// What the portal granted on the last Start, empty when it granted nothing to keep.
	std::string const& GetRestoreToken() const { return m_restoreToken; }

	Capture::CAudioStream const& GetSystemAudio() const { return m_systemAudio; }
	Capture::CAudioStream const& GetMicrophoneAudio() const { return m_microphoneAudio; }

private:

	struct SSlot final
	{
		std::vector<uint8_t>  pixels;
		uint32_t              stride{ 0 };
		uint32_t              width{ 0 };
		uint32_t              height{ 0 };
		uint64_t              timestampNs{ 0 };
		Capture::EPixelFormat format{ Capture::EPixelFormat::BGRx };
	};

	struct SAnswer final
	{
		Capture::SPortalGrant grant;
		StartedCallback       callback;
	};

	void Continue(SAnswer const& answer);
	void NoteEncodeFailure();
	void OnFrame(Capture::SFrame const& frame);
	void OnAudio(uint32_t source, Capture::SAudioBuffer const& buffer);

	bool OpenAudio();
	void EncoderMain();
	bool StartEncoder(Capture::SFrame const& frame);

	Capture::CPortalSession  m_portal;
	Capture::CPipeWireStream m_stream;
	Capture::CAudioStream    m_systemAudio;
	Capture::CAudioStream    m_microphoneAudio;
	Encode::CEncoder         m_encoder;

	std::vector<SSlot>                                 m_slots;
	Tge::Threading::CMpscQueue<uint32_t>               m_filled;
	Tge::Threading::CMpscQueue<uint32_t>               m_free;
	Tge::Threading::CMpscQueue<SAnswer>                m_answers;
	std::counting_semaphore<>                          m_filledCount{ 0 };
	std::thread                                        m_encoderThread;

	EndedCallback m_onEnded;
	WakeCallback  m_wake;

	SRecordingRequest m_request;

	std::string m_startFailure;
	std::string m_restoreToken;

	uint32_t m_numAudioSources{ 0 };
	uint32_t m_systemSource{ InvalidSource };
	uint32_t m_microphoneSource{ InvalidSource };

	std::atomic<uint64_t> m_lastFrameTimestampNs{ 0 };
	std::atomic<uint64_t> m_lastFrameArrivalNs{ 0 };
	std::atomic<uint64_t> m_numDropped{ 0 };
	std::atomic<uint64_t> m_numEncodeFailures{ 0 };
	std::atomic<bool>     m_stopping{ false };
	std::atomic<bool>     m_encoding{ false };
	std::atomic<bool>     m_startFailed{ false };

	SSessionStats m_lastStats;
};
} // namespace Klip::Recorder
