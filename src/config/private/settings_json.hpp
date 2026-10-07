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

std::string WriteSettingsJson(Recorder::SSettings const& settings, std::string_view kept);

std::expected<SSettingsDocument, ESettingsJsonError> ReadSettingsJson(std::string_view text,
                                                                      Recorder::SSettings const& defaults);
std::string DescribeSettingsSyntaxError(std::string_view text);
} // namespace Klip::Config
