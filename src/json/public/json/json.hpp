#pragma once

namespace Klip::Json
{
[[noreturn]] void AbortOnJsonError(char const* pWhat);
} // namespace Klip::Json

#define JSON_USE_IMPLICIT_CONVERSIONS 0
#define JSON_THROW_USER(exception) ::Klip::Json::AbortOnJsonError((exception).what())

#include <nlohmann/json.hpp>
