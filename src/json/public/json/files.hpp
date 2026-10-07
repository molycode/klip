#pragma once

#include <cstddef>
#include <expected>
#include <filesystem>
#include <string>
#include <string_view>
#include <system_error>

namespace Klip::Json
{
std::expected<std::string, std::error_code> ReadFile(std::filesystem::path const& path, size_t maxSize);
std::expected<void, std::string> WriteFileAtomically(std::filesystem::path const& path, std::string_view text);
} // namespace Klip::Json
