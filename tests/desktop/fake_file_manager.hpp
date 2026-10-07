#pragma once

#include "bus/connection.hpp"

#include <systemd/sd-bus.h>
#include <tge/non_copyable.hpp>

#include <string>
#include <vector>

namespace Klip::Tests
{
class CFakeFileManager final : private Tge::SNoCopyNoMove
{
public:

	CFakeFileManager() = default;
	~CFakeFileManager() = default;

	bool Initialize();
	void Terminate();

	std::vector<std::string> TakeShown();

	int OnShowItems(sd_bus_message* pCall);

private:

	Bus::CConnection         m_connection;
	std::vector<std::string> m_shown;
	sd_bus_slot*             m_pSlot{ nullptr };
};
} // namespace Klip::Tests
