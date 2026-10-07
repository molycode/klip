#pragma once

#include <string>
#include <string_view>

namespace Klip::Json
{
std::string DescribeSyntaxError(std::string_view text, bool ignoreComments);
} // namespace Klip::Json
