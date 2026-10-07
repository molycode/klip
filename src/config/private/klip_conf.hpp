#pragma once

#include <string>
#include <string_view>

namespace Klip::Config
{
std::string ConvertKlipConf(std::string_view text);
} // namespace Klip::Config
