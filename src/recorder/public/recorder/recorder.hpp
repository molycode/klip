#pragma once

#include "capture/audio_device.hpp"
#include "capture/audio_devices.hpp"
#include "capture/audio_stream.hpp"
#include "desktop/request.hpp"
#include "desktop/tray.hpp"
#include "encode/capabilities.hpp"
#include "encode/format.hpp"
#include "encode/quality.hpp"
#include "encode/settings.hpp"
#include "recorder/audio_source.hpp"
#include "recorder/level_ballistics.hpp"
#include "recorder/reveal.hpp"
#include "recorder/screen.hpp"
#include "recorder/session.hpp"
#include "recorder/settings.hpp"
#include "recorder/settings_changes.hpp"
#include "recorder/source.hpp"
#include "recorder/state.hpp"

#include <tge/non_copyable.hpp>

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace Klip::Recorder
{
class CRecorder final : private Tge::SNoCopyNoMove
{
public:

	using TimePoint = std::chrono::steady_clock::time_point;

	using WakeCallback = std::function<void()>;

	CRecorder() = default;
	~CRecorder() = default;

	bool Initialize(SSettings settings, SScreen const& screen, Encode::SCapabilities const& capabilities,
	                Desktop::STrayIcons icons, Desktop::RequestCallback onRequest, WakeCallback wake);
	void Terminate();

	void Update();
	std::optional<TimePoint> GetNextDeadline() const;

	void SetSource(ESource source);
	void SetRememberWindow(bool remember);
	void SetDirectory(std::string directory);
	void SetContainer(Encode::EContainer container);
	void SetCodec(Encode::ECodec codec);
	void SetQuality(Encode::EQuality quality);
	void SetMaxFrameRate(uint32_t maxFrameRate);
	void SetAudioQuality(Encode::EQuality quality);
	void SetAudioEnabled(EAudioSource source, bool enabled);
	void SetAudioDevice(EAudioSource source, std::string nodeName);
	void SetGain(EAudioSource source, int decibels);

	void SetScreen(SScreen const& screen) { m_screen = screen; }

	void SetVisible(bool visible);
	void RefreshAudioDevices();

	bool PrepareRecording();
	void BeginRecording(Encode::SRegion const& region);
	void RequestCapture();
	void CancelRecording();
	void StopRecording();

	EReveal          TakeReveal();
	SSettingsChanges TakeSettingsChanges();

	SSettings const& GetSettings() const { return m_settings; }
	EState           GetState() const { return m_state; }

	std::span<Encode::EContainer const> GetContainers() const { return m_containers; }
	std::span<Encode::ECodec const>     GetCodecs() const { return m_codecs; }

	bool CarriesAudio() const;

	std::span<Capture::SAudioDevice const> GetDevices(EAudioSource source) const;

	CLevelBallistics const& GetMeter(EAudioSource source) const { return m_meters[Index(source)]; }

	bool IsTrayAvailable() const { return m_tray.IsAvailable(); }

	std::string const& GetStatus() const { return m_status; }
	std::string const& GetElapsed() const { return m_elapsed; }
	std::string const& GetQualityHint() const { return m_qualityHint; }
	std::string const& GetAudioQualityHint() const { return m_audioQualityHint; }
	std::string const& GetLastPath() const { return m_currentPath; }

private:

	static constexpr size_t Index(EAudioSource source) { return static_cast<size_t>(source); }

	void RefreshCodecs();
	void RefreshMonitoring();
	void StopMonitoring();
	void UpdateMeter(EAudioSource source, Capture::CAudioStream const& stream, TimePoint now);
	void OnMeterTick(TimePoint now);
	void OnTick(TimePoint now);
	void UpdateQualityHint();
	void UpdateAudioQualityHint();
	void ShowIdleState(std::string message);
	void OnStarted(bool started, bool keeps, Capture::ESourceType source, std::string const& sent);

	bool WantsAudio(EAudioSource source) const;

	Capture::SAudioDevice FindDevice(EAudioSource source) const;

	std::string MakeOutputPath() const;

	CSession               m_session;
	Desktop::CTray         m_tray;
	Capture::CAudioDevices m_audioDevices;

	std::array<Capture::CAudioStream, NumAudioSources> m_monitors;
	std::array<CLevelBallistics, NumAudioSources>      m_meters;

	std::array<std::string, NumAudioSources> m_monitored;
	std::array<uint64_t, NumAudioSources>    m_numBuffers{};

	std::array<std::string, NumAudioSources> m_writtenDevices;

	std::vector<Encode::EContainer> m_containers;
	std::vector<Encode::ECodec>     m_codecs;

	SSettings             m_settings;
	SSettingsChanges      m_changes;
	SScreen               m_screen;
	Encode::SCapabilities m_capabilities;
	WakeCallback     m_wake;

	std::string m_status;
	std::string m_elapsed;
	std::string m_qualityHint;
	std::string m_audioQualityHint;
	std::string m_currentPath;

	Encode::SRegion m_region;

	std::optional<TimePoint> m_meterDeadline;
	std::optional<TimePoint> m_tickDeadline;

	TimePoint                                m_recordingStart;
	std::optional<std::chrono::milliseconds> m_firstByte;

	EState  m_state{ EState::Idle };
	EReveal m_reveal{ EReveal::None };
	bool    m_visible{ false };

	std::atomic<bool> m_withdrawn{ false };
};
} // namespace Klip::Recorder
