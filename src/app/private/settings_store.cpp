#include "settings_store.hpp"

#include <QtCore/QSettings>
#include <QtCore/QStandardPaths>
#include <QtCore/QString>

#include <string>
#include <string_view>

namespace Klip
{
namespace
{
constexpr char const* DirectoryKey{ "output/directory" };
constexpr char const* ContainerKey{ "output/container" };
constexpr char const* CodecKey{ "output/codec" };
constexpr char const* QualityKey{ "output/quality" };
constexpr char const* SourceKey{ "capture/source" };
constexpr char const* FrameRateKey{ "capture/frameRate" };
constexpr char const* RememberWindowKey{ "capture/rememberWindow" };
constexpr char const* SystemAudioKey{ "audio/systemEnabled" };
constexpr char const* SystemDeviceKey{ "audio/systemDevice" };
constexpr char const* MicrophoneKey{ "audio/microphoneEnabled" };
constexpr char const* MicrophoneDeviceKey{ "audio/microphoneDevice" };
constexpr char const* AudioQualityKey{ "audio/quality" };
constexpr char const* SystemGainKey{ "audio/systemGain" };
constexpr char const* MicrophoneGainKey{ "audio/microphoneGain" };
constexpr char const* ScreenTokenKey{ "portal/restoreToken/screen" };
constexpr char const* WindowTokenKey{ "portal/restoreToken/window" };

constexpr size_t System{ static_cast<size_t>(Recorder::EAudioSource::System) };
constexpr size_t Microphone{ static_cast<size_t>(Recorder::EAudioSource::Microphone) };

//////////////////////////////////////////////////////////////////////////
QString ToQString(std::string_view text)
{
	return QString::fromUtf8(text.data(), static_cast<qsizetype>(text.size()));
}

//////////////////////////////////////////////////////////////////////////
std::string ReadString(QSettings const& settings, char const* key, QString const& fallback = {})
{
	return settings.value(key, fallback).toString().toStdString();
}

//////////////////////////////////////////////////////////////////////////
void WriteToken(QSettings& settings, char const* key, std::string const& token)
{
	if (token.empty())
	{
		settings.remove(key);
	}
	else
	{
		settings.setValue(key, QString::fromStdString(token));
	}
}
} // namespace

//////////////////////////////////////////////////////////////////////////
Recorder::SSettings LoadSettings()
{
	QSettings const     settings;
	Recorder::SSettings result;

	QString const fallbackDirectory{ QStandardPaths::writableLocation(QStandardPaths::MoviesLocation) +
		                             QStringLiteral("/klip-captures") };

	result.directory = ReadString(settings, DirectoryKey, fallbackDirectory);

	int const source{ settings.value(SourceKey, 0).toInt() };

	result.source = source >= static_cast<int>(Recorder::ESource::Screen) &&
	                        source <= static_cast<int>(Recorder::ESource::Region)
	                    ? static_cast<Recorder::ESource>(source)
	                    : Recorder::ESource::Screen;

	result.rememberWindow = settings.value(RememberWindowKey, false).toBool();
	result.container = Encode::ParseContainer(ReadString(settings, ContainerKey, QStringLiteral("mp4")),
	                                          Encode::EContainer::Mp4);
	result.codec = Encode::ParseCodec(ReadString(settings, CodecKey, QStringLiteral("h264")), Encode::ECodec::H264);
	result.quality = Encode::ParseQuality(ReadString(settings, QualityKey, QStringLiteral("balanced")),
	                                      Encode::EQuality::Balanced);
	result.maxFrameRate = static_cast<uint32_t>(settings.value(FrameRateKey, 0).toInt());

	result.audio[System].enabled = settings.value(SystemAudioKey, false).toBool();
	result.audio[System].device = ReadString(settings, SystemDeviceKey);
	result.audio[System].gainDecibels = settings.value(SystemGainKey, 0).toInt();

	result.audio[Microphone].enabled = settings.value(MicrophoneKey, false).toBool();
	result.audio[Microphone].device = ReadString(settings, MicrophoneDeviceKey);
	result.audio[Microphone].gainDecibels = settings.value(MicrophoneGainKey, 0).toInt();

	result.audioQuality = Encode::ParseQuality(ReadString(settings, AudioQualityKey, QStringLiteral("high")),
	                                           Encode::EQuality::High);

	result.screenToken = ReadString(settings, ScreenTokenKey);
	result.windowToken = ReadString(settings, WindowTokenKey);

	return result;
}

//////////////////////////////////////////////////////////////////////////
void SaveSettings(Recorder::SSettings const& settings, Recorder::SSettingsChanges const& changes)
{
	if (changes.choices || changes.screenToken || changes.windowToken)
	{
		QSettings file;

		if (changes.choices)
		{
			file.setValue(DirectoryKey, QString::fromStdString(settings.directory));
			file.setValue(SourceKey, static_cast<int>(settings.source));
			file.setValue(RememberWindowKey, settings.rememberWindow);
			file.setValue(ContainerKey, ToQString(Encode::GetContainerName(settings.container)));
			file.setValue(CodecKey, ToQString(Encode::GetCodecName(settings.codec)));
			file.setValue(QualityKey, ToQString(Encode::GetQualityName(settings.quality)));
			file.setValue(FrameRateKey, static_cast<int>(settings.maxFrameRate));
			file.setValue(SystemAudioKey, settings.audio[System].enabled);
			file.setValue(SystemDeviceKey, QString::fromStdString(settings.audio[System].device));
			file.setValue(MicrophoneKey, settings.audio[Microphone].enabled);
			file.setValue(MicrophoneDeviceKey, QString::fromStdString(settings.audio[Microphone].device));
			file.setValue(AudioQualityKey, ToQString(Encode::GetQualityName(settings.audioQuality)));
			file.setValue(SystemGainKey, settings.audio[System].gainDecibels);
			file.setValue(MicrophoneGainKey, settings.audio[Microphone].gainDecibels);
		}

		if (changes.screenToken)
		{
			WriteToken(file, ScreenTokenKey, settings.screenToken);
		}

		if (changes.windowToken)
		{
			WriteToken(file, WindowTokenKey, settings.windowToken);
		}
	}
}
} // namespace Klip
