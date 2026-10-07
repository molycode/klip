#pragma once

#include <string>
#include <string_view>

namespace Klip::Bus
{
std::string ToFileUri(std::string_view path);
std::string ToLocalPath(std::string_view uri);
} // namespace Klip::Bus
