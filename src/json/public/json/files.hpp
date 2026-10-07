#pragma once

#include <cstddef>
#include <expected>
#include <filesystem>
#include <string>
#include <string_view>
#include <system_error>

namespace Klip::Json
{
// A regular file of at most maxSize bytes; anything else is an error.
std::expected<std::string, std::error_code> ReadFile(std::filesystem::path const& path, size_t maxSize);
// Through a temporary file of its own, so neither a crash nor a second writer can tear it; the error says what
// failed.
std::expected<void, std::string> WriteFileAtomically(std::filesystem::path const& path, std::string_view text);
} // namespace Klip::Json
