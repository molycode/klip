#include "menu.hpp"

#include <algorithm>
#include <string>

namespace Klip::Desktop
{
//////////////////////////////////////////////////////////////////////////
std::vector<Bus::SOption> GetMenuProperties(int32_t id, bool recording)
{
	std::vector<Bus::SOption> properties;

	switch (id)
	{
		case ToggleId:
			properties = { { "label", std::string{ recording ? "Stop recording" : "Start recording" } },
			               { "enabled", true },
			               { "visible", true } };
			break;

		case ShowId:
			properties = { { "label", std::string{ "Show Klip" } }, { "enabled", true }, { "visible", true } };
			break;

		case SeparatorId:
			properties = { { "type", std::string{ "separator" } }, { "visible", true } };
			break;

		case QuitId:
			properties = { { "label", std::string{ "Quit" } }, { "enabled", true }, { "visible", true } };
			break;

		default:
			properties = { { "children-display", std::string{ "submenu" } } };
			break;
	}

	return properties;
}

//////////////////////////////////////////////////////////////////////////
// (ia{sv}av), with the root's items as children when depth allows.
int AppendMenuLayout(sd_bus_message* pMessage, int32_t id, int32_t depth, bool recording)
{
	int result{ sd_bus_message_open_container(pMessage, 'r', "ia{sv}av") };

	if (result >= 0)
	{
		result = sd_bus_message_append(pMessage, "i", id);
	}

	if (result >= 0)
	{
		result = Bus::AppendOptions(pMessage, GetMenuProperties(id, recording));
	}

	if (result >= 0)
	{
		result = sd_bus_message_open_container(pMessage, 'a', "v");
	}

	if (id == RootId && depth != 0)
	{
		for (int32_t const child : MenuItemIds)
		{
			if (result >= 0)
			{
				result = sd_bus_message_open_container(pMessage, 'v', "(ia{sv}av)");
			}

			if (result >= 0)
			{
				result = AppendMenuLayout(pMessage, child, 0, recording);
			}

			if (result >= 0)
			{
				result = sd_bus_message_close_container(pMessage);
			}
		}
	}

	if (result >= 0)
	{
		result = sd_bus_message_close_container(pMessage);
	}

	if (result >= 0)
	{
		result = sd_bus_message_close_container(pMessage);
	}

	return result;
}

//////////////////////////////////////////////////////////////////////////
bool FindMenuProperty(int32_t id, bool recording, std::string_view name, Bus::SOption& found)
{
	std::vector<Bus::SOption> const properties{ GetMenuProperties(id, recording) };
	auto const match{ std::ranges::find_if(properties,
	                                       [name](Bus::SOption const& option) { return option.pKey == name; }) };
	bool const exists{ match != properties.end() };

	if (exists)
	{
		found = *match;
	}

	return exists;
}
} // namespace Klip::Desktop
