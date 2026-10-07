#pragma once

#include "recorder/settings.hpp"
#include "settings_document.hpp"
#include "settings_json_error.hpp"

#include <cstdint>
#include <expected>
#include <string>
#include <string_view>

namespace Klip::Config
{
inline constexpr uint32_t SettingsVersion{ 1 };

// Over kept, the text last read or written, so the keys this Klip does not know survive it.
std::string WriteSettingsJson(Recorder::SSettings const& settings, std::string_view kept);

// Only text that is not a JSON object fails as a whole; an invalid value keeps its default and is counted.
std::expected<SSettingsDocument, ESettingsJsonError> ReadSettingsJson(std::string_view text,
                                                                      Recorder::SSettings const& defaults);
// Where text that ReadSettingsJson found not to be JSON goes wrong.
std::string DescribeSettingsSyntaxError(std::string_view text);
} // namespace Klip::Config
