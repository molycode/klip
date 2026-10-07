#pragma once

#include <span>
#include <string_view>

namespace Klip
{
struct SLicensedComponent final
{
	std::string_view                name;
	std::string_view                licence;
	std::span<unsigned char const> text;
};
} // namespace Klip
