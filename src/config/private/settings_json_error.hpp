#pragma once

#include <cstdint>
#include <string_view>

namespace Klip::Config
{
enum class ESettingsJsonError : uint8_t
{
	NotJson,
	NotAnObject
};

constexpr std::string_view ToString(ESettingsJsonError error)
{
	std::string_view text{ "unknown error" };

	switch (error)
	{
		case ESettingsJsonError::NotJson:
			text = "not valid JSON";
			break;
		case ESettingsJsonError::NotAnObject:
			text = "not a JSON object";
			break;
	}

	return text;
}
} // namespace Klip::Config
