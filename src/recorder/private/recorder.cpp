#include "recorder/recorder.hpp"

#include "encode/capabilities.hpp"
#include "log.hpp"
#include "recorder/decibels.hpp"
#include "recorder/format.hpp"
#include "recorder/frame_rates.hpp"

#include <tge/assert.hpp>

#include <algorithm>
#include <cmath>
#include <ctime>
#include <filesystem>
#include <format>
#include <system_error>
#include <utility>

namespace Klip::Recorder
{
namespace
{
constexpr std::chrono::milliseconds TickInterval{ 250 };
constexpr std::chrono::milliseconds MeterInterval{ 40 };

constexpr uint32_t FallbackFrameRate{ 60 };

constexpr double BytesPerMebibyte{ 1024.0 * 1024.0 };

//////////////////////////////////////////////////////////////////////////
// Klip's own trim per source. The system's volumes are left alone: a sink's does not reach its monitor at all,
// and a source's belongs to every other application too.
float ToGain(int decibels)
{
	return FromDecibels(static_cast<float>(decibels));
}

//////////////////////////////////////////////////////////////////////////
std::string MakeStamp()
{
	std::time_t const now{ std::time(nullptr) };
	std::tm           local{};

	localtime_r(&now, &local);

	return std::format("{:04}{:02}{:02}-{:02}{:02}{:02}", local.tm_year + 1900, local.tm_mon + 1, local.tm_mday,
	                   local.tm_hour, local.tm_min, local.tm_sec);
}

//////////////////////////////////////////////////////////////////////////
// An empty folder is the working directory, as QDir has always read it.
std::filesystem::path ToFolder(std::string const& directory)
{
	return directory.empty() ? std::filesystem::path{ "." } : std::filesystem::path{ directory };
}
} // namespace

//////////////////////////////////////////////////////////////////////////
// Every call but the wake belongs to the thread that owns the recorder; the wake, from any thread, asks only
// that Update be called there.
bool CRecorder::Initialize(SSettings settings, SScreen const& screen, Desktop::STrayIcons icons,
                           Desktop::RequestCallback onRequest, WakeCallback wake)
{
	m_settings = std::move(settings);
	m_screen = screen;
	m_wake = std::move(wake);

	if (!m_audioDevices.Initialize())
	{
		gLog.Warning("No audio devices could be listed; Klip will record silent.");
	}

	for (size_t index{ 0 }; index < Encode::ContainerCount; ++index)
	{
		Encode::EContainer const container{ static_cast<Encode::EContainer>(index) };

		bool usable{ false };

		for (size_t codecIndex{ 0 }; codecIndex < Encode::CodecCount; ++codecIndex)
		{
			Encode::ECodec const codec{ static_cast<Encode::ECodec>(codecIndex) };

			usable = usable || (Encode::ContainerAccepts(container, codec) && Encode::IsCodecOffered(codec));
		}

		if (usable)
		{
			m_containers.push_back(container);
		}
	}

	TGE_ASSERT(!m_containers.empty(), "MP4 takes every codec, and some codec is always offered");

	if (std::ranges::find(m_containers, m_settings.container) == m_containers.end())
	{
		m_settings.container = m_containers.front();
	}

	RefreshCodecs();

	if (std::ranges::find(FrameRateCaps, m_settings.maxFrameRate) == FrameRateCaps.end())
	{
		m_settings.maxFrameRate = NoFrameRateCap;
	}

	for (size_t index{ 0 }; index < NumAudioSources; ++index)
	{
		SAudioChoice& choice{ m_settings.audio[index] };

		choice.gainDecibels = std::clamp(choice.gainDecibels, MinimumGainDecibels, MaximumGainDecibels);
		m_writtenDevices[index] = choice.device;
	}

	RefreshAudioDevices();

	m_status = "Ready";
	m_elapsed = FormatDuration(std::chrono::milliseconds{ 0 });

	UpdateAudioQualityHint();
	UpdateQualityHint();

	m_tray.Initialize(std::move(icons), std::move(onRequest));

	// On the PipeWire or the bus thread, and it can run twice.
	m_session.SetEndedCallback([this]() {
		m_withdrawn.store(true, std::memory_order_release);
		m_wake();
	});

	return m_session.Initialize(m_wake);
}

//////////////////////////////////////////////////////////////////////////
void CRecorder::Terminate()
{
	if (m_state == EState::Recording)
	{
		StopRecording();
	}

	StopMonitoring();

	m_tray.Terminate();

	m_session.Terminate();
	m_audioDevices.Terminate();
}

//////////////////////////////////////////////////////////////////////////
// After every wake, and once GetNextDeadline has passed.
void CRecorder::Update()
{
	m_session.Update();

	// Taken whether or not it still applies, so a late second call cannot stop the next recording.
	if (m_withdrawn.exchange(false, std::memory_order_acq_rel) && m_state == EState::Recording)
	{
		StopRecording();
		m_status = "Screen sharing was stopped";
	}

	TimePoint const now{ std::chrono::steady_clock::now() };

	if (m_meterDeadline.has_value() && now >= *m_meterDeadline)
	{
		m_meterDeadline = now + MeterInterval;
		OnMeterTick(now);
	}

	if (m_tickDeadline.has_value() && now >= *m_tickDeadline)
	{
		m_tickDeadline = now + TickInterval;
		OnTick(now);
	}
}

//////////////////////////////////////////////////////////////////////////
std::optional<CRecorder::TimePoint> CRecorder::GetNextDeadline() const
{
	std::optional<TimePoint> next{ m_meterDeadline };

	if (m_tickDeadline.has_value() && (!next.has_value() || *m_tickDeadline < *next))
	{
		next = m_tickDeadline;
	}

	return next;
}

//////////////////////////////////////////////////////////////////////////
void CRecorder::SetSource(ESource source)
{
	m_settings.source = source;
	m_changes.choices = true;

	UpdateQualityHint();
}

//////////////////////////////////////////////////////////////////////////
void CRecorder::SetRememberWindow(bool remember)
{
	m_settings.rememberWindow = remember;
	m_changes.choices = true;
}

//////////////////////////////////////////////////////////////////////////
void CRecorder::SetDirectory(std::string directory)
{
	m_settings.directory = std::move(directory);
	m_changes.choices = true;
}

//////////////////////////////////////////////////////////////////////////
void CRecorder::SetContainer(Encode::EContainer container)
{
	m_settings.container = container;
	m_changes.choices = true;

	RefreshCodecs();
	RefreshMonitoring();
	UpdateQualityHint();
}

//////////////////////////////////////////////////////////////////////////
void CRecorder::SetCodec(Encode::ECodec codec)
{
	m_settings.codec = codec;
	m_changes.choices = true;

	UpdateQualityHint();
}

//////////////////////////////////////////////////////////////////////////
void CRecorder::SetQuality(Encode::EQuality quality)
{
	m_settings.quality = quality;
	m_changes.choices = true;

	UpdateQualityHint();
}

//////////////////////////////////////////////////////////////////////////
void CRecorder::SetMaxFrameRate(uint32_t maxFrameRate)
{
	m_settings.maxFrameRate = maxFrameRate;
	m_changes.choices = true;

	UpdateQualityHint();
}

//////////////////////////////////////////////////////////////////////////
void CRecorder::SetAudioQuality(Encode::EQuality quality)
{
	m_settings.audioQuality = quality;
	m_changes.choices = true;

	UpdateAudioQualityHint();
	UpdateQualityHint();
}

//////////////////////////////////////////////////////////////////////////
void CRecorder::SetAudioEnabled(EAudioSource source, bool enabled)
{
	m_settings.audio[Index(source)].enabled = enabled;
	m_changes.choices = true;

	RefreshMonitoring();
	UpdateQualityHint();
}

//////////////////////////////////////////////////////////////////////////
void CRecorder::SetAudioDevice(EAudioSource source, std::string nodeName)
{
	m_settings.audio[Index(source)].device = std::move(nodeName);
	m_changes.choices = true;

	RefreshMonitoring();
}

//////////////////////////////////////////////////////////////////////////
void CRecorder::SetGain(EAudioSource source, int decibels)
{
	m_settings.audio[Index(source)].gainDecibels = std::clamp(decibels, MinimumGainDecibels, MaximumGainDecibels);
	m_changes.choices = true;
}

//////////////////////////////////////////////////////////////////////////
// The previews run only while the window is on screen.
void CRecorder::SetVisible(bool visible)
{
	m_visible = visible;

	RefreshMonitoring();
}

//////////////////////////////////////////////////////////////////////////
// A device that went away falls back to the first one, and that is not the user's choice: nothing is written.
void CRecorder::RefreshAudioDevices()
{
	std::array<std::string, NumAudioSources> wanted;

	for (size_t index{ 0 }; index < NumAudioSources; ++index)
	{
		EAudioSource const source{ static_cast<EAudioSource>(index) };

		wanted[index] = GetDevices(source).empty() ? m_writtenDevices[index] : m_settings.audio[index].device;
	}

	m_audioDevices.Refresh();

	for (size_t index{ 0 }; index < NumAudioSources; ++index)
	{
		std::span<Capture::SAudioDevice const> const devices{ GetDevices(static_cast<EAudioSource>(index)) };

		bool const found{ std::ranges::any_of(devices, [&wanted, index](Capture::SAudioDevice const& device) {
			return device.nodeName == wanted[index];
		}) };

		std::string& device{ m_settings.audio[index].device };

		if (found)
		{
			device = wanted[index];
		}
		else if (!devices.empty())
		{
			device = devices.front().nodeName;
		}
		else
		{
			device.clear();
		}
	}
}

//////////////////////////////////////////////////////////////////////////
// A start runs in steps, so the window can get out of shot between them.
// Refused without a word unless idle: a tray toggle can arrive while the window is still on its way out.
bool CRecorder::PrepareRecording()
{
	bool prepared{ false };

	if (m_state == EState::Idle)
	{
		std::filesystem::path const folder{ ToFolder(m_settings.directory) };
		std::error_code             error;

		m_region = Encode::SRegion{};

		std::filesystem::create_directories(folder, error);

		prepared = !error && std::filesystem::is_directory(folder, error);

		if (prepared)
		{
			m_state = EState::Starting;
		}
		else
		{
			gLog.Error("Cannot create or write to {}", m_settings.directory);
			ShowIdleState("Cannot write to that folder");
		}
	}

	return prepared;
}

//////////////////////////////////////////////////////////////////////////
void CRecorder::BeginRecording(Encode::SRegion const& region)
{
	TGE_ASSERT(m_state == EState::Starting, "BeginRecording comes only after a PrepareRecording that succeeded");

	m_region = region;
	m_currentPath = MakeOutputPath();

	StopMonitoring();

	m_status = "Waiting for permission…";
}

//////////////////////////////////////////////////////////////////////////
void CRecorder::RequestCapture()
{
	TGE_ASSERT(m_state == EState::Starting, "RequestCapture comes only after a PrepareRecording that succeeded");

	SRecordingRequest request;
	request.outputPath = m_currentPath;
	request.codec = m_settings.codec;
	request.quality = m_settings.quality;
	request.maxFrameRate = m_settings.maxFrameRate;
	request.audioQuality = m_settings.audioQuality;
	request.systemGain = ToGain(m_settings.audio[Index(EAudioSource::System)].gainDecibels);
	request.microphoneGain = ToGain(m_settings.audio[Index(EAudioSource::Microphone)].gainDecibels);

	if (CarriesAudio())
	{
		if (WantsAudio(EAudioSource::System))
		{
			request.systemAudio = FindDevice(EAudioSource::System);
		}

		if (WantsAudio(EAudioSource::Microphone))
		{
			request.microphone = FindDevice(EAudioSource::Microphone);
		}
	}

	request.region = m_region;
	request.rememberWindow = m_settings.rememberWindow;
	request.source = m_settings.source == ESource::Window ? Capture::ESourceType::Window
	                                                      : Capture::ESourceType::Screen;

	bool const   keeps{ Capture::KeepsGrant(request.source, request.rememberWindow) };
	bool const   window{ request.source == Capture::ESourceType::Window };
	std::string& token{ window ? m_settings.windowToken : m_settings.screenToken };

	if (!keeps)
	{
		token.clear();
		(window ? m_changes.windowToken : m_changes.screenToken) = true;
	}

	request.restoreToken = token;

	m_session.Start(request, [this, keeps, source = request.source, sent = request.restoreToken](bool started) {
		OnStarted(started, keeps, source, sent);
	});
}

//////////////////////////////////////////////////////////////////////////
void CRecorder::OnStarted(bool started, bool keeps, Capture::ESourceType source, std::string const& sent)
{
	std::string const& granted{ m_session.GetRestoreToken() };

	// Whether or not it started: the portal spent the token it was sent.
	if (keeps && !granted.empty() && granted != sent)
	{
		bool const window{ source == Capture::ESourceType::Window };

		(window ? m_settings.windowToken : m_settings.screenToken) = granted;
		(window ? m_changes.windowToken : m_changes.screenToken) = true;
	}

	if (started)
	{
		TimePoint const now{ std::chrono::steady_clock::now() };

		m_recordingStart = now;
		m_firstByte.reset();
		m_tickDeadline = now + TickInterval;
		// From the portal's grant, before the first frame -- and without one, if the encoder cannot start.
		m_state = EState::Recording;
		m_status = "Recording";
		m_tray.SetRecording(true);

		RefreshMonitoring();
	}
	else
	{
		gLog.Warning("The recording did not start.");
		m_reveal = EReveal::Show;

		std::string const& failure{ m_session.GetStartFailure() };

		ShowIdleState(failure.empty() ? std::string{ "Recording was not permitted" }
		                              : std::format("{} is unavailable — pick another device", failure));
	}
}

//////////////////////////////////////////////////////////////////////////
void CRecorder::CancelRecording()
{
	ShowIdleState("Ready");
}

//////////////////////////////////////////////////////////////////////////
void CRecorder::StopRecording()
{
	m_tickDeadline.reset();

	bool const          written{ m_session.Stop() };
	SSessionStats const stats{ m_session.GetStats() };

	if (written)
	{
		gLog.Info("Captured {}, encoded {}, dropped {}", stats.numCaptured, stats.numEncoded, stats.numDropped);
		ShowIdleState(std::format("Saved {}", std::filesystem::path{ m_currentPath }.filename().string()));
	}
	else
	{
		gLog.Error("The recording could not be finalised; {} may be unusable.", m_currentPath);
		ShowIdleState("Recording failed");
	}

	m_elapsed = FormatDuration(std::chrono::milliseconds{ 0 });
	m_reveal = EReveal::Raise;

	RefreshMonitoring();
}

//////////////////////////////////////////////////////////////////////////
EReveal CRecorder::TakeReveal()
{
	return std::exchange(m_reveal, EReveal::None);
}

//////////////////////////////////////////////////////////////////////////
// What the settings file holds for each device, which a refresh looks for while the list is empty.
SSettingsChanges CRecorder::TakeSettingsChanges()
{
	if (m_changes.choices)
	{
		for (size_t index{ 0 }; index < NumAudioSources; ++index)
		{
			m_writtenDevices[index] = m_settings.audio[index].device;
		}
	}

	return std::exchange(m_changes, SSettingsChanges{});
}

//////////////////////////////////////////////////////////////////////////
bool CRecorder::CarriesAudio() const
{
	return Encode::ContainerCarriesAudio(m_settings.container);
}

//////////////////////////////////////////////////////////////////////////
std::span<Capture::SAudioDevice const> CRecorder::GetDevices(EAudioSource source) const
{
	return source == EAudioSource::System ? m_audioDevices.GetSinks() : m_audioDevices.GetSources();
}

//////////////////////////////////////////////////////////////////////////
void CRecorder::RefreshCodecs()
{
	m_codecs.clear();

	for (size_t index{ 0 }; index < Encode::CodecCount; ++index)
	{
		Encode::ECodec const codec{ static_cast<Encode::ECodec>(index) };

		if (Encode::ContainerAccepts(m_settings.container, codec) && Encode::IsCodecOffered(codec))
		{
			m_codecs.push_back(codec);
		}
	}

	if (std::ranges::find(m_codecs, m_settings.codec) == m_codecs.end() && !m_codecs.empty())
	{
		m_settings.codec = m_codecs.front();
	}
}

//////////////////////////////////////////////////////////////////////////
void CRecorder::RefreshMonitoring()
{
	bool const canMonitor{ m_visible && m_state == EState::Idle && CarriesAudio() };

	for (size_t index{ 0 }; index < NumAudioSources; ++index)
	{
		EAudioSource const source{ static_cast<EAudioSource>(index) };
		std::string const  wanted{ canMonitor && WantsAudio(source) ? m_settings.audio[index].device : std::string{} };

		if (wanted != m_monitored[index])
		{
			m_monitors[index].Terminate();
			m_monitored[index].clear();
			m_numBuffers[index] = 0;
			m_meters[index].Reset();

			if (!wanted.empty() && m_monitors[index].Initialize(FindDevice(source), {}, {}))
			{
				m_monitored[index] = wanted;
			}

			m_meters[index].SetUnavailable(!wanted.empty() && m_monitored[index].empty());
		}
	}

	bool const idle{ std::ranges::all_of(m_monitored, [](std::string const& node) { return node.empty(); }) };

	if (idle && m_state != EState::Recording)
	{
		m_meterDeadline.reset();
	}
	else
	{
		m_meterDeadline = std::chrono::steady_clock::now() + MeterInterval;
	}
}

//////////////////////////////////////////////////////////////////////////
void CRecorder::StopMonitoring()
{
	m_meterDeadline.reset();

	for (size_t index{ 0 }; index < NumAudioSources; ++index)
	{
		m_monitors[index].Terminate();
		m_monitored[index].clear();
		m_numBuffers[index] = 0;
		m_meters[index].Reset();
		m_meters[index].SetUnavailable(false);
	}
}

//////////////////////////////////////////////////////////////////////////
void CRecorder::UpdateMeter(EAudioSource source, Capture::CAudioStream const& stream, TimePoint now)
{
	size_t const   index{ Index(source) };
	uint64_t const current{ stream.GetNumBuffers() };
	float const    gain{ ToGain(m_settings.audio[index].gainDecibels) };

	std::array<float, Capture::MaxAudioChannels> peaks{};

	for (uint32_t channel{ 0 }; channel < Capture::MaxAudioChannels; ++channel)
	{
		// No new buffer means no new sound, not the last one held forever.
		peaks[channel] = current != m_numBuffers[index] ? stream.GetChannelPeak(channel) * gain : 0.0f;
	}

	m_numBuffers[index] = current;
	m_meters[index].Feed(peaks, now);
}

//////////////////////////////////////////////////////////////////////////
void CRecorder::OnMeterTick(TimePoint now)
{
	// While recording the session owns the devices, so the meters read what is being written rather than a
	// preview that is no longer open.
	bool const recording{ m_state == EState::Recording };

	if (recording || !m_monitored[Index(EAudioSource::System)].empty())
	{
		UpdateMeter(EAudioSource::System,
		            recording ? m_session.GetSystemAudio() : m_monitors[Index(EAudioSource::System)], now);
	}

	if (recording || !m_monitored[Index(EAudioSource::Microphone)].empty())
	{
		UpdateMeter(EAudioSource::Microphone,
		            recording ? m_session.GetMicrophoneAudio() : m_monitors[Index(EAudioSource::Microphone)], now);
	}
}

//////////////////////////////////////////////////////////////////////////
void CRecorder::OnTick(TimePoint now)
{
	SSessionStats const stats{ m_session.GetStats() };

	auto const elapsed{ std::chrono::duration_cast<std::chrono::milliseconds>(now - m_recordingStart) };

	// Bytes trail the clock while the encode and mux pipeline fills, and dividing by the wall clock would read
	// low for the whole recording rather than only at the start.
	if (!m_firstByte.has_value() && stats.bytesWritten > 0)
	{
		m_firstByte = elapsed;
	}

	std::chrono::milliseconds const writing{ m_firstByte.has_value() ? elapsed - *m_firstByte
	                                                                 : std::chrono::milliseconds{ 0 } };

	std::string const elapsedText{ FormatDuration(elapsed) };
	std::string const written{ FormatBytes(stats.bytesWritten) };
	SThroughput const rate{ FormatThroughput(stats.bytesWritten,
	                                         std::chrono::duration_cast<std::chrono::seconds>(writing)) };

	std::string const detail{ rate.perMinute.empty()
		                          ? written
		                          : std::format("{} · {} · {}", written, rate.perMinute, rate.perHour) };

	// GNOME's indicator extension renders the label but declines to render a tooltip, so this is the only
	// figure visible while recording -- and the window is hidden then, so it is the hourly one.
	std::string label{ rate.perHour.empty() ? elapsedText : std::format("{} · {}", elapsedText, rate.perHour) };

	m_elapsed = elapsedText;
	m_status = detail;
	m_tray.SetLabel(std::move(label));
	m_tray.SetDetail(std::format("{} · {}", elapsedText, detail));
}

//////////////////////////////////////////////////////////////////////////
void CRecorder::UpdateQualityHint()
{
	// Uncapped settles at the panel's refresh rate, which is not always 60.
	uint32_t const cap{ m_settings.maxFrameRate };
	uint32_t const rate{ cap != NoFrameRateCap ? cap
	                                           : (m_screen.refreshHz != 0 ? m_screen.refreshHz : FallbackFrameRate) };

	// Only a whole screen has a size before the portal answers; a window or a region is whatever the user is
	// about to point at.
	bool const knowsSize{ m_settings.source == ESource::Screen };

	// An upper bound: the rate is the most the compositor delivers, and a screen sends frames only where it changes.
	uint64_t bitsPerSecond{ Encode::EstimateBitsPerSecond(m_settings.codec, m_settings.quality, m_screen.width,
	                                                      m_screen.height, rate) };

	if (CarriesAudio() && (WantsAudio(EAudioSource::System) || WantsAudio(EAudioSource::Microphone)))
	{
		bitsPerSecond += static_cast<uint64_t>(Encode::GetAudioBitsPerSecond(m_settings.audioQuality));
	}

	double const mebibytesPerMinute{ static_cast<double>(bitsPerSecond) * 60.0 / 8.0 / BytesPerMebibyte };

	// Measured at 8.5x the table on a game, so this is nothing like a ceiling and must not read as one.
	m_qualityHint = knowsSize
		                ? std::format("around {:.1f} MiB per minute at {}x{}, more for video or games",
		                              mebibytesPerMinute, m_screen.width, m_screen.height)
		                : std::format("around {:.1f} MiB per minute for a whole screen, less for a smaller area",
		                              mebibytesPerMinute);
}

//////////////////////////////////////////////////////////////////////////
void CRecorder::UpdateAudioQualityHint()
{
	int const bitsPerSecond{ Encode::GetAudioBitsPerSecond(m_settings.audioQuality) };

	m_audioQualityHint = std::format("AAC, {} kbps stereo", bitsPerSecond / 1000);
}

//////////////////////////////////////////////////////////////////////////
void CRecorder::ShowIdleState(std::string message)
{
	m_state = EState::Idle;
	m_status = std::move(message);

	m_tray.SetRecording(false);
}

//////////////////////////////////////////////////////////////////////////
bool CRecorder::WantsAudio(EAudioSource source) const
{
	return m_settings.audio[Index(source)].enabled && !GetDevices(source).empty();
}

//////////////////////////////////////////////////////////////////////////
Capture::SAudioDevice CRecorder::FindDevice(EAudioSource source) const
{
	Capture::SAudioDevice device;

	for (Capture::SAudioDevice const& candidate : GetDevices(source))
	{
		if (candidate.nodeName == m_settings.audio[Index(source)].device)
		{
			device = candidate;
		}
	}

	return device;
}

//////////////////////////////////////////////////////////////////////////
std::string CRecorder::MakeOutputPath() const
{
	std::string const name{ std::format("klip-{}.{}", MakeStamp(),
	                                    Encode::GetContainerExtension(m_settings.container)) };

	return (ToFolder(m_settings.directory) / name).string();
}
} // namespace Klip::Recorder
