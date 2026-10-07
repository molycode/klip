#pragma once

#include <string>
#include <string_view>

namespace Klip::Config
{
// What QSettings wrote as Klip.conf, as config.json text for ReadSettingsJson to judge: a value Klip could not
// have written arrives there as something that fails its check.
std::string ConvertKlipConf(std::string_view text);
} // namespace Klip::Config
