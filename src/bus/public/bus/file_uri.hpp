#pragma once

#include <string>
#include <string_view>

namespace Klip::Bus
{
// Every byte but an unreserved one or '/' percent-encoded.
std::string ToFileUri(std::string_view path);
// Empty unless the URI names a local file: file:// with no host or localhost, and no malformed escape or NUL.
std::string ToLocalPath(std::string_view uri);
} // namespace Klip::Bus
