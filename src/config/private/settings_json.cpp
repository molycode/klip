#include "settings_json.hpp"

#include "encode/format.hpp"
#include "encode/quality.hpp"
#include "json/json.hpp"
#include "json/syntax_error.hpp"
#include "recorder/audio_choice.hpp"
#include "recorder/audio_source.hpp"
#include "recorder/frame_rates.hpp"
#include "recorder/source.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <format>
#include <limits>
#include <string>
#include <string_view>
#include <utility>

namespace Klip::Config
{
namespace
{
using JsonValue = nlohmann::ordered_json;

constexpr int  IndentWidth{ 1 };
constexpr char IndentCharacter{ '\t' };
constexpr bool EnsureAscii{ false };
constexpr bool AllowExceptions{ false };
constexpr bool IgnoreComments{ true };

constexpr std::array<std::string_view, Recorder::NumAudioSources> AudioKeys{ "system", "microphone" };

struct SSourceName final
{
	Recorder::ESource source;
	std::string_view  name;
};

constexpr std::array SourceNames
{
	SSourceName{ Recorder::ESource::Screen, "screen" },
	SSourceName{ Recorder::ESource::Window, "window" },
	SSourceName{ Recorder::ESource::Region, "region" }
};

//////////////////////////////////////////////////////////////////////////
constexpr std::string_view GetSourceName(Recorder::ESource source)
{
	std::string_view name{ "unknown" };

	for (SSourceName const& entry : SourceNames)
	{
		if (entry.source == source)
		{
			name = entry.name;
		}
	}

	return name;
}

//////////////////////////////////////////////////////////////////////////
constexpr Recorder::ESource ParseSource(std::string_view name, Recorder::ESource fallback)
{
	Recorder::ESource source{ fallback };

	for (SSourceName const& entry : SourceNames)
	{
		if (entry.name == name)
		{
			source = entry.source;
		}
	}

	return source;
}

//////////////////////////////////////////////////////////////////////////
std::string JoinPath(std::string_view parent, std::string_view key)
{
	return parent.empty() ? std::string{ key } : std::format("{}.{}", parent, key);
}

//////////////////////////////////////////////////////////////////////////
void Reject(SSettingsDocument& document, std::string path)
{
	if (document.numInvalid == 0)
	{
		document.firstInvalidPath = std::move(path);
	}

	++document.numInvalid;
}

//////////////////////////////////////////////////////////////////////////
// nlohmann keeps a non-negative integer as unsigned, and get<int64_t>() would wrap one past INT64_MAX.
bool IsIntegerInRange(JsonValue const& json, int min, int max)
{
	bool isInRange{ false };

	if (json.is_number_unsigned())
	{
		uint64_t const number{ json.get<uint64_t>() };

		isInRange = std::cmp_greater_equal(number, min) && std::cmp_less_equal(number, max);
	}
	else if (json.is_number_integer())
	{
		int64_t const number{ json.get<int64_t>() };

		isInRange = number >= min && number <= max;
	}

	return isInRange;
}

//////////////////////////////////////////////////////////////////////////
template<typename TRead>
void ReadValue(JsonValue const& object, std::string_view parent, std::string_view key, SSettingsDocument& document,
               TRead&& read)
{
	JsonValue::const_iterator const it{ object.find(key) };

	if (it != object.cend() && !read(*it))
	{
		Reject(document, JoinPath(parent, key));
	}
}

//////////////////////////////////////////////////////////////////////////
JsonValue const* FindObject(JsonValue const& object, std::string_view parent, std::string_view key,
                            SSettingsDocument& document)
{
	JsonValue const* pFound{ nullptr };

	ReadValue(object, parent, key, document, [&pFound](JsonValue const& json) {
		bool const isObject{ json.is_object() };

		if (isObject)
		{
			pFound = &json;
		}

		return isObject;
	});

	return pFound;
}

//////////////////////////////////////////////////////////////////////////
void ReadBool(JsonValue const& object, std::string_view parent, std::string_view key, bool& value,
              SSettingsDocument& document)
{
	ReadValue(object, parent, key, document, [&value](JsonValue const& json) {
		bool const isValid{ json.is_boolean() };

		if (isValid)
		{
			value = json.get<bool>();
		}

		return isValid;
	});
}

//////////////////////////////////////////////////////////////////////////
// A NUL would silently cut the string short wherever it later becomes a C string, such as a path.
void ReadString(JsonValue const& object, std::string_view parent, std::string_view key, std::string& value,
                SSettingsDocument& document)
{
	ReadValue(object, parent, key, document, [&value](JsonValue const& json) {
		bool const isValid{ json.is_string() && !json.get_ref<std::string const&>().contains('\0') };

		if (isValid)
		{
			value = json.get<std::string>();
		}

		return isValid;
	});
}

//////////////////////////////////////////////////////////////////////////
// Read back through getName, since parse answers its fallback for a name it does not know.
template<typename TEnum>
void ReadName(JsonValue const& object, std::string_view parent, std::string_view key,
              TEnum (*parse)(std::string_view, TEnum), std::string_view (*getName)(TEnum), TEnum& value,
              SSettingsDocument& document)
{
	ReadValue(object, parent, key, document, [parse, getName, &value](JsonValue const& json) {
		bool isValid{ json.is_string() };

		if (isValid)
		{
			std::string const& name{ json.get_ref<std::string const&>() };
			TEnum const        parsed{ parse(name, value) };

			isValid = getName(parsed) == name;

			if (isValid)
			{
				value = parsed;
			}
		}

		return isValid;
	});
}

//////////////////////////////////////////////////////////////////////////
void ReadAudioChoice(JsonValue const& object, std::string_view path, Recorder::SAudioChoice& choice,
                     SSettingsDocument& document)
{
	ReadBool(object, path, "enabled", choice.enabled, document);
	ReadString(object, path, "device", choice.device, document);

	ReadValue(object, path, "gain", document, [&choice](JsonValue const& json) {
		bool const isValid{ IsIntegerInRange(json, Recorder::MinimumGainDecibels, Recorder::MaximumGainDecibels) };

		if (isValid)
		{
			choice.gainDecibels = static_cast<int>(json.get<int64_t>());
		}

		return isValid;
	});
}

//////////////////////////////////////////////////////////////////////////
void ReadDocument(JsonValue const& root, SSettingsDocument& document)
{
	Recorder::SSettings& settings{ document.settings };

	ReadValue(root, {}, "version", document, [&document](JsonValue const& json) {
		bool const isValid{ json.is_number_unsigned() &&
		                    json.get<uint64_t>() <= std::numeric_limits<uint32_t>::max() };

		if (isValid)
		{
			document.version = static_cast<uint32_t>(json.get<uint64_t>());
		}

		return isValid;
	});

	JsonValue const* const pOutput{ FindObject(root, {}, "output", document) };

	if (pOutput != nullptr)
	{
		ReadString(*pOutput, "output", "directory", settings.directory, document);
		ReadName(*pOutput, "output", "container", Encode::ParseContainer, Encode::GetContainerName,
		         settings.container, document);
		ReadName(*pOutput, "output", "codec", Encode::ParseCodec, Encode::GetCodecName, settings.codec, document);
		ReadName(*pOutput, "output", "quality", Encode::ParseQuality, Encode::GetQualityName, settings.quality,
		         document);
	}

	JsonValue const* const pCapture{ FindObject(root, {}, "capture", document) };

	if (pCapture != nullptr)
	{
		ReadName(*pCapture, "capture", "source", ParseSource, GetSourceName, settings.source, document);
		ReadBool(*pCapture, "capture", "rememberWindow", settings.rememberWindow, document);

		ReadValue(*pCapture, "capture", "frameRate", document, [&settings](JsonValue const& json) {
			bool const isValid{ json.is_number_unsigned() &&
			                    std::ranges::find(Recorder::FrameRateCaps, json.get<uint64_t>()) !=
			                        Recorder::FrameRateCaps.end() };

			if (isValid)
			{
				settings.maxFrameRate = static_cast<uint32_t>(json.get<uint64_t>());
			}

			return isValid;
		});
	}

	JsonValue const* const pAudio{ FindObject(root, {}, "audio", document) };

	if (pAudio != nullptr)
	{
		for (size_t index{ 0 }; index < Recorder::NumAudioSources; ++index)
		{
			JsonValue const* const pChoice{ FindObject(*pAudio, "audio", AudioKeys[index], document) };

			if (pChoice != nullptr)
			{
				ReadAudioChoice(*pChoice, JoinPath("audio", AudioKeys[index]), settings.audio[index], document);
			}
		}

		ReadName(*pAudio, "audio", "quality", Encode::ParseQuality, Encode::GetQualityName, settings.audioQuality,
		         document);
	}

	JsonValue const* const pPortal{ FindObject(root, {}, "portal", document) };
	JsonValue const* const pTokens{ (pPortal != nullptr) ? FindObject(*pPortal, "portal", "restoreTokens", document)
	                                                     : nullptr };

	if (pTokens != nullptr)
	{
		ReadString(*pTokens, "portal.restoreTokens", "screen", settings.screenToken, document);
		ReadString(*pTokens, "portal.restoreTokens", "window", settings.windowToken, document);
	}
}

//////////////////////////////////////////////////////////////////////////
// Never brace-initialised: nlohmann makes a json from one braced value a one-element array.
JsonValue ParseKept(std::string_view kept)
{
	JsonValue root = JsonValue::parse(kept, nullptr, AllowExceptions, IgnoreComments);

	if (!root.is_object())
	{
		root = JsonValue::object();
	}

	return root;
}

//////////////////////////////////////////////////////////////////////////
// A section the file holds as anything else was read as missing, so it is replaced.
// Members live in a vector: a reference this returns dies once its parent gains a key.
JsonValue& GetSection(JsonValue& parent, std::string_view key)
{
	JsonValue& section{ parent[key] };

	if (!section.is_object())
	{
		section = JsonValue::object();
	}

	return section;
}
} // namespace

//////////////////////////////////////////////////////////////////////////
// Over kept, the text last read or written, so the keys this Klip does not know survive it.
std::string WriteSettingsJson(Recorder::SSettings const& settings, std::string_view kept)
{
	JsonValue root = ParseKept(kept);

	root["version"] = SettingsVersion;

	JsonValue& output{ GetSection(root, "output") };

	output["directory"] = settings.directory;
	output["container"] = Encode::GetContainerName(settings.container);
	output["codec"] = Encode::GetCodecName(settings.codec);
	output["quality"] = Encode::GetQualityName(settings.quality);

	JsonValue& capture{ GetSection(root, "capture") };

	capture["source"] = GetSourceName(settings.source);
	capture["frameRate"] = settings.maxFrameRate;
	capture["rememberWindow"] = settings.rememberWindow;

	JsonValue& audio{ GetSection(root, "audio") };

	for (size_t index{ 0 }; index < Recorder::NumAudioSources; ++index)
	{
		JsonValue& choice{ GetSection(audio, AudioKeys[index]) };

		choice["enabled"] = settings.audio[index].enabled;
		choice["device"] = settings.audio[index].device;
		choice["gain"] = settings.audio[index].gainDecibels;
	}

	audio["quality"] = Encode::GetQualityName(settings.audioQuality);

	JsonValue& tokens{ GetSection(GetSection(root, "portal"), "restoreTokens") };

	tokens["screen"] = settings.screenToken;
	tokens["window"] = settings.windowToken;

	// Replacing invalid UTF-8 rather than failing, which without exceptions would be an abort.
	return root.dump(IndentWidth, IndentCharacter, EnsureAscii, JsonValue::error_handler_t::replace) + '\n';
}

//////////////////////////////////////////////////////////////////////////
// Only text that is not a JSON object fails as a whole; an invalid value keeps its default and is counted.
std::expected<SSettingsDocument, ESettingsJsonError> ReadSettingsJson(std::string_view text,
                                                                      Recorder::SSettings const& defaults)
{
	std::expected<SSettingsDocument, ESettingsJsonError> result{ std::unexpected{ ESettingsJsonError::NotJson } };

	JsonValue const root = JsonValue::parse(text, nullptr, AllowExceptions, IgnoreComments);

	if (root.is_object())
	{
		SSettingsDocument document{};

		document.settings = defaults;
		document.version = SettingsVersion;
		ReadDocument(root, document);
		result = std::move(document);
	}
	else if (!root.is_discarded())
	{
		result = std::unexpected{ ESettingsJsonError::NotAnObject };
	}

	return result;
}

//////////////////////////////////////////////////////////////////////////
std::string DescribeSettingsSyntaxError(std::string_view text)
{
	return Json::DescribeSyntaxError(text, IgnoreComments);
}
} // namespace Klip::Config
