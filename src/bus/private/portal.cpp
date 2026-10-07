#include "bus/portal.hpp"

#include <systemd/sd-bus.h>

#include <algorithm>
#include <atomic>
#include <format>
#include <system_error>

namespace Klip::Bus
{
namespace
{
int AppendVariant(sd_bus_message* pMessage, std::string const& value)
{
	return sd_bus_message_append(pMessage, "v", "s", value.c_str());
}

int AppendVariant(sd_bus_message* pMessage, uint32_t value)
{
	return sd_bus_message_append(pMessage, "v", "u", value);
}

int AppendVariant(sd_bus_message* pMessage, bool value)
{
	// sd-bus takes a boolean as an int through the varargs.
	return sd_bus_message_append(pMessage, "v", "b", static_cast<int>(value));
}

int ReadEntry(sd_bus_message* pMessage, DictVisitor const& visitor)
{
	char const* pKey{ nullptr };
	int result{ sd_bus_message_read(pMessage, "s", &pKey) };

	if (result >= 0 && !visitor(pKey, pMessage))
	{
		result = sd_bus_message_skip(pMessage, "v");
	}

	return result;
}
} // namespace

//////////////////////////////////////////////////////////////////////////
std::string MakeHandleToken()
{
	// The request path carries the connection's unique name, so a count is unique enough.
	static std::atomic<uint32_t> counter{ 0 };

	return std::format("klip_{}", counter.fetch_add(1, std::memory_order_relaxed) + 1);
}

//////////////////////////////////////////////////////////////////////////
// A portal answers on a Request object at this path, so its Response can be subscribed before the call.
std::string GetRequestPath(sd_bus* pBus, std::string_view token)
{
	char const* pUniqueName{ nullptr };
	std::string sender;

	if (sd_bus_get_unique_name(pBus, &pUniqueName) >= 0)
	{
		sender = pUniqueName;
		sender.erase(0, sender.starts_with(':') ? 1 : 0);
		std::ranges::replace(sender, '.', '_');
	}

	return std::format("{}/request/{}/{}", PortalPath, sender, token);
}

//////////////////////////////////////////////////////////////////////////
int AppendOptions(sd_bus_message* pMessage, std::vector<SOption> const& options)
{
	int result{ sd_bus_message_open_container(pMessage, 'a', "{sv}") };

	for (SOption const& option : options)
	{
		if (result >= 0)
		{
			result = sd_bus_message_open_container(pMessage, 'e', "sv");
		}

		if (result >= 0)
		{
			result = sd_bus_message_append(pMessage, "s", option.pKey);
		}

		if (result >= 0)
		{
			result = AppendValue(pMessage, option);
		}

		if (result >= 0)
		{
			result = sd_bus_message_close_container(pMessage);
		}
	}

	if (result >= 0)
	{
		result = sd_bus_message_close_container(pMessage);
	}

	return result;
}

//////////////////////////////////////////////////////////////////////////
int AppendValue(sd_bus_message* pMessage, SOption const& option)
{
	return std::visit([pMessage](auto const& value) { return AppendVariant(pMessage, value); }, option.value);
}

//////////////////////////////////////////////////////////////////////////
// The visitor reads the variant of each key it wants and answers true; every other entry is skipped.
int ReadDict(sd_bus_message* pMessage, DictVisitor const& visitor)
{
	int result{ sd_bus_message_enter_container(pMessage, 'a', "{sv}") };

	if (result >= 0)
	{
		int entered{ 1 };

		while (result >= 0 && entered > 0)
		{
			entered = sd_bus_message_enter_container(pMessage, 'e', "sv");

			if (entered > 0)
			{
				result = ReadEntry(pMessage, visitor);
			}
			else
			{
				result = entered;
			}

			if (result >= 0 && entered > 0)
			{
				result = sd_bus_message_exit_container(pMessage);
			}
		}

		if (result >= 0)
		{
			result = sd_bus_message_exit_container(pMessage);
		}
	}

	return result;
}

//////////////////////////////////////////////////////////////////////////
// The portal hands some values over as a string and some as an object path.
bool ReadString(sd_bus_message* pMessage, std::string& value)
{
	char type{ 0 };
	char const* pContents{ nullptr };
	char const* pValue{ nullptr };
	bool read{ false };

	if (sd_bus_message_peek_type(pMessage, &type, &pContents) > 0 && type == SD_BUS_TYPE_VARIANT
	    && pContents != nullptr && (std::string_view{ pContents } == "s" || std::string_view{ pContents } == "o")
	    && sd_bus_message_read(pMessage, "v", pContents, &pValue) >= 0)
	{
		value = pValue;
		read = true;
	}

	return read;
}

//////////////////////////////////////////////////////////////////////////
std::string Describe(sd_bus_error const& error, int result)
{
	return error.message != nullptr ? std::string{ error.message } : std::generic_category().message(-result);
}
} // namespace Klip::Bus
