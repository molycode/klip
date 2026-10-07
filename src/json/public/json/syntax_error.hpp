#pragma once

#include <string>
#include <string_view>

namespace Klip::Json
{
// Where and why the text stops being JSON ("line 3, column 5: syntax error while parsing …"); empty for valid
// JSON.
// ignoreComments as the parse that failed had it, or a comment would be named as the error.
std::string DescribeSyntaxError(std::string_view text, bool ignoreComments);
} // namespace Klip::Json
