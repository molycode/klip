#pragma once

#include <array>
#include <cstddef>
#include <format>
#include <string_view>
#include <utility>

namespace Klip
{
template<size_t Size, typename... TArgs>
std::string_view FormatTo(std::array<char, Size>& buffer, std::format_string<TArgs...> format, TArgs&&... args)
{
	static_assert(Size > 1, "FormatTo needs room for at least one character and the terminator");

	auto const result{ std::format_to_n(buffer.data(), static_cast<std::ptrdiff_t>(Size - 1), format,
	                                    std::forward<TArgs>(args)...) };

	*result.out = '\0';

	return std::string_view{ buffer.data(), result.out };
}
} // namespace Klip
