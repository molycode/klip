#pragma once

#include <systemd/sd-bus.h>

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

// The shape every org.freedesktop.portal request shares: a handle token chosen by the caller, a Request object
// whose path follows from it, and the answer arriving later as that object's Response signal.
namespace Klip::Bus
{
inline constexpr char const* PortalService{ "org.freedesktop.portal.Desktop" };
inline constexpr char const* PortalPath{ "/org/freedesktop/portal/desktop" };
inline constexpr char const* RequestInterface{ "org.freedesktop.portal.Request" };

// Fixed by the portal specification.
inline constexpr uint32_t ResponseSuccess{ 0 };
inline constexpr uint32_t ResponseCancelled{ 1 };

struct SOption final
{
	char const*                                  pKey{ nullptr };
	std::variant<std::string, uint32_t, bool> value;
};

std::string MakeHandleToken();

// Where the portal publishes the Request for a token, so its Response can be subscribed before the call.
std::string GetRequestPath(sd_bus* pBus, std::string_view token);

// As one a{sv}.
int AppendOptions(sd_bus_message* pMessage, std::vector<SOption> const& options);

// The visitor reads the variant of each key it wants and answers true; every other entry is skipped.
using DictVisitor = std::function<bool(std::string_view key, sd_bus_message* pMessage)>;

int ReadDict(sd_bus_message* pMessage, DictVisitor const& visitor);

// A variant holding a string or an object path: the portal hands some values over as one and some as the other.
bool ReadString(sd_bus_message* pMessage, std::string& value);

std::string Describe(sd_bus_error const& error, int result);
} // namespace Klip::Bus
