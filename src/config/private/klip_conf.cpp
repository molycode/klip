#include "klip_conf.hpp"

#include "json/json.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <system_error>
#include <utility>

namespace Klip::Config
{
namespace
{
using JsonValue = nlohmann::ordered_json;

enum class EKind : uint8_t
{
	String,
	Boolean,
	Integer,
	Source
};

struct SLegacyKey final
{
	std::string_view section;
	std::string_view key;
	std::string_view pointer;
	EKind            kind;
};

// QSettings writes the '/' inside a key as '\'.
constexpr std::array LegacyKeys
{
	SLegacyKey{ "output",  "directory",            "/output/directory",            EKind::String  },
	SLegacyKey{ "output",  "container",            "/output/container",            EKind::String  },
	SLegacyKey{ "output",  "codec",                "/output/codec",                EKind::String  },
	SLegacyKey{ "output",  "quality",              "/output/quality",              EKind::String  },
	SLegacyKey{ "capture", "source",               "/capture/source",              EKind::Source  },
	SLegacyKey{ "capture", "frameRate",            "/capture/frameRate",           EKind::Integer },
	SLegacyKey{ "capture", "rememberWindow",       "/capture/rememberWindow",      EKind::Boolean },
	SLegacyKey{ "audio",   "systemEnabled",        "/audio/system/enabled",        EKind::Boolean },
	SLegacyKey{ "audio",   "systemDevice",         "/audio/system/device",         EKind::String  },
	SLegacyKey{ "audio",   "systemGain",           "/audio/system/gain",           EKind::Integer },
	SLegacyKey{ "audio",   "microphoneEnabled",    "/audio/microphone/enabled",    EKind::Boolean },
	SLegacyKey{ "audio",   "microphoneDevice",     "/audio/microphone/device",     EKind::String  },
	SLegacyKey{ "audio",   "microphoneGain",       "/audio/microphone/gain",       EKind::Integer },
	SLegacyKey{ "audio",   "quality",              "/audio/quality",               EKind::String  },
	SLegacyKey{ "portal",  "restoreToken\\screen", "/portal/restoreTokens/screen", EKind::String  },
	SLegacyKey{ "portal",  "restoreToken\\window", "/portal/restoreTokens/window", EKind::String  }
};

// capture/source held the source as its number then.
constexpr std::array<std::string_view, 3> LegacySources{ "screen", "window", "region" };

struct SEscape final
{
	char code;
	char value;
};

constexpr std::array Escapes
{
	SEscape{ 'a', '\a' }, SEscape{ 'b', '\b' }, SEscape{ 'f', '\f' }, SEscape{ 'n', '\n' },
	SEscape{ 'r', '\r' }, SEscape{ 't', '\t' }, SEscape{ 'v', '\v' }, SEscape{ '"', '"' },
	SEscape{ '?', '?' },  SEscape{ '\'', '\'' }, SEscape{ '\\', '\\' }
};

//////////////////////////////////////////////////////////////////////////
std::string_view Trim(std::string_view text)
{
	constexpr std::string_view Blanks{ " \t\r" };

	size_t const first{ text.find_first_not_of(Blanks) };

	return (first == std::string_view::npos) ? std::string_view{}
	                                         : text.substr(first, text.find_last_not_of(Blanks) - first + 1);
}

//////////////////////////////////////////////////////////////////////////
// QSettings' quoting and escapes undone, and the '@' it doubles at the start of a string. Numeric escapes and
// an unquoted comma (a list) come only from characters and types Klip never stored, so they fail instead.
std::optional<std::string> Unescape(std::string_view raw)
{
	std::string value{};
	bool        isValid{ true };
	bool        isQuoted{ false };
	size_t      index{ 0 };

	while (isValid && index < raw.size())
	{
		char const ch{ raw[index] };

		if (ch == '\\')
		{
			auto const pEscape{ (index + 1 < raw.size()) ? std::ranges::find(Escapes, raw[index + 1], &SEscape::code)
			                                             : Escapes.end() };

			isValid = pEscape != Escapes.end();

			if (isValid)
			{
				value += pEscape->value;
			}

			index += 2;
		}
		else if (ch == '"')
		{
			isQuoted = !isQuoted;
			++index;
		}
		else
		{
			isValid = isQuoted || ch != ',';
			value += ch;
			++index;
		}
	}

	if (value.starts_with("@@"))
	{
		value.erase(0, 1);
	}

	return isValid ? std::optional<std::string>{ std::move(value) } : std::nullopt;
}

//////////////////////////////////////////////////////////////////////////
std::optional<int64_t> ParseInteger(std::string_view text)
{
	int64_t number{ 0 };
	auto const [pEnd, error]{ std::from_chars(text.data(), text.data() + text.size(), number) };

	return (error == std::errc{} && pEnd == text.data() + text.size()) ? std::optional<int64_t>{ number }
	                                                                   : std::nullopt;
}

//////////////////////////////////////////////////////////////////////////
// Left a string where it is not what QSettings writes for its type, so the reader rejects it.
JsonValue Convert(EKind kind, std::string const& text)
{
	JsonValue value = text;
	std::optional<int64_t> const number{ ParseInteger(text) };

	switch (kind)
	{
		case EKind::String:
			break;

		case EKind::Boolean:
			if (text == "true" || text == "false")
			{
				value = text == "true";
			}
			break;

		case EKind::Integer:
			if (number.has_value())
			{
				value = *number;
			}
			break;

		case EKind::Source:
			if (number.has_value() && *number >= 0 && std::cmp_less(*number, LegacySources.size()))
			{
				value = LegacySources[static_cast<size_t>(*number)];
			}
			break;
	}

	return value;
}
} // namespace

//////////////////////////////////////////////////////////////////////////
std::string ConvertKlipConf(std::string_view text)
{
	JsonValue        root = JsonValue::object();
	std::string_view section{};
	size_t           start{ 0 };

	while (start < text.size())
	{
		size_t const           end{ std::min(text.find('\n', start), text.size()) };
		std::string_view const line{ Trim(text.substr(start, end - start)) };
		size_t const           equals{ line.find('=') };

		if (line.starts_with('[') && line.ends_with(']'))
		{
			section = line.substr(1, line.size() - 2);
		}
		else if (equals != std::string_view::npos)
		{
			std::string_view const key{ Trim(line.substr(0, equals)) };
			auto const pLegacy{ std::ranges::find_if(LegacyKeys, [section, key](SLegacyKey const& legacy) {
				return legacy.section == section && legacy.key == key;
			}) };

			if (pLegacy != LegacyKeys.end())
			{
				std::optional<std::string> const value{ Unescape(Trim(line.substr(equals + 1))) };

				root[JsonValue::json_pointer{ std::string{ pLegacy->pointer } }] =
					value.has_value() ? Convert(pLegacy->kind, *value) : JsonValue{};
			}
		}

		start = end + 1;
	}

	return root.dump();
}
} // namespace Klip::Config
