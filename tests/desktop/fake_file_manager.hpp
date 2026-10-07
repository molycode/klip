#pragma once

#include "bus/connection.hpp"

#include <systemd/sd-bus.h>
#include <tge/non_copyable.hpp>

#include <string>
#include <vector>

namespace Klip::Tests
{
// org.freedesktop.FileManager1 on a connection and a thread of its own; what it records is touched only there.
class CFakeFileManager final : private Tge::SNoCopyNoMove
{
public:

	CFakeFileManager() = default;
	~CFakeFileManager() = default;

	bool Initialize();
	void Terminate();

	// The URIs every ShowItems call named, oldest first.
	std::vector<std::string> TakeShown();

	// sd-bus calls this; public only so its plain function pointer can reach it.
	int OnShowItems(sd_bus_message* pCall);

private:

	Bus::CConnection         m_connection;
	std::vector<std::string> m_shown;
	sd_bus_slot*             m_pSlot{ nullptr };
};
} // namespace Klip::Tests
