#include "desktop/file_manager.hpp"

#include "bus/connection.hpp"

#include <systemd/sd-bus.h>

#include <string>

namespace Klip::Desktop
{
namespace
{
constexpr char const* Service{ "org.freedesktop.FileManager1" };
constexpr char const* ObjectPath{ "/org/freedesktop/FileManager1" };
} // namespace

//////////////////////////////////////////////////////////////////////////
// False where the desktop has no org.freedesktop.FileManager1.
bool ShowInFileManager(std::string_view fileUri)
{
	std::string const uri{ fileUri };
	bool              shown{ false };

	Bus::gConnection.Run([&uri, &shown](sd_bus* pBus) {
		shown = sd_bus_call_method(pBus, Service, ObjectPath, Service, "ShowItems", nullptr, nullptr, "ass", 1,
		                           uri.c_str(), "") >= 0;
	});

	return shown;
}
} // namespace Klip::Desktop
