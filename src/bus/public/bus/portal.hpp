#pragma once

#include <systemd/sd-bus.h>

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace Klip::Bus
{
inline constexpr char const* PortalService{ "org.freedesktop.portal.Desktop" };
inline constexpr char const* PortalPath{ "/org/freedesktop/portal/desktop" };
inline constexpr char const* RequestInterface{ "org.freedesktop.portal.Request" };

inline constexpr uint32_t ResponseSuccess{ 0 };
inline constexpr uint32_t ResponseCancelled{ 1 };

struct SOption final
{
	char const*                                  pKey{ nullptr };
	std::variant<std::string, uint32_t, bool> value;
};

std::string MakeHandleToken();

std::string GetRequestPath(sd_bus* pBus, std::string_view token);

int AppendOptions(sd_bus_message* pMessage, std::vector<SOption> const& options);

int AppendValue(sd_bus_message* pMessage, SOption const& option);

using DictVisitor = std::function<bool(std::string_view key, sd_bus_message* pMessage)>;

int ReadDict(sd_bus_message* pMessage, DictVisitor const& visitor);

bool ReadString(sd_bus_message* pMessage, std::string& value);

std::string Describe(sd_bus_error const& error, int result);
} // namespace Klip::Bus
