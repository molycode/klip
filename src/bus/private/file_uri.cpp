#include "bus/file_uri.hpp"

#include <cstddef>
#include <format>
#include <iterator>

namespace Klip::Bus
{
namespace
{
constexpr std::string_view Scheme{ "file://" };
constexpr std::string_view LocalHost{ "localhost" };

//////////////////////////////////////////////////////////////////////////
constexpr bool IsKeptAsIs(unsigned char byte)
{
	return (byte >= 'A' && byte <= 'Z') || (byte >= 'a' && byte <= 'z') || (byte >= '0' && byte <= '9') ||
	       byte == '-' || byte == '.' || byte == '_' || byte == '~' || byte == '/';
}

//////////////////////////////////////////////////////////////////////////
// -1 for anything but a hexadecimal digit.
constexpr int GetHexValue(char character)
{
	int value{ -1 };

	if (character >= '0' && character <= '9')
	{
		value = character - '0';
	}
	else if (character >= 'A' && character <= 'F')
	{
		value = character - 'A' + 10;
	}
	else if (character >= 'a' && character <= 'f')
	{
		value = character - 'a' + 10;
	}

	return value;
}
} // namespace

//////////////////////////////////////////////////////////////////////////
std::string ToFileUri(std::string_view path)
{
	std::string uri{ Scheme };

	for (char const character : path)
	{
		unsigned char const byte{ static_cast<unsigned char>(character) };

		if (IsKeptAsIs(byte))
		{
			uri += character;
		}
		else
		{
			std::format_to(std::back_inserter(uri), "%{:02X}", byte);
		}
	}

	return uri;
}

//////////////////////////////////////////////////////////////////////////
std::string ToLocalPath(std::string_view uri)
{
	std::string      path{};
	std::string_view rest{ uri.starts_with(Scheme) ? uri.substr(Scheme.size()) : std::string_view{} };

	if (rest.starts_with(LocalHost))
	{
		rest.remove_prefix(LocalHost.size());
	}

	bool   isValid{ rest.starts_with('/') };
	size_t index{ 0 };

	while (isValid && index < rest.size())
	{
		if (rest[index] == '%')
		{
			int const high{ (index + 2 < rest.size()) ? GetHexValue(rest[index + 1]) : -1 };
			int const low{ (index + 2 < rest.size()) ? GetHexValue(rest[index + 2]) : -1 };

			isValid = high >= 0 && low >= 0 && high * 16 + low > 0;
			path += static_cast<char>(high * 16 + low);
			index += 3;
		}
		else
		{
			path += rest[index];
			++index;
		}
	}

	return isValid ? path : std::string{};
}
} // namespace Klip::Bus
