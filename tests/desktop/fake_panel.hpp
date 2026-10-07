#pragma once

#include "bus/connection.hpp"

#include <systemd/sd-bus.h>
#include <tge/non_copyable.hpp>

#include <string>
#include <vector>

namespace Klip::Tests
{
struct SRecordedSignal final
{
	std::string member;
	std::string signature;

	std::string toggleLabel;
};

class CFakePanel final : private Tge::SNoCopyNoMove
{
public:

	CFakePanel() = default;
	~CFakePanel() = default;

	bool Initialize();
	void Terminate();

	Bus::CConnection& GetConnection() { return m_connection; }

	void Withdraw();
	bool Restore();

	std::string GetRegistered();

	void Follow(std::string const& service);
	std::vector<SRecordedSignal> TakeSignals();

	int OnRegister(sd_bus_message* pCall);
	int OnSignal(sd_bus_message* pMessage);

private:

	Bus::CConnection             m_connection;
	std::vector<SRecordedSignal> m_signals;
	std::string                  m_registered;
	sd_bus_slot*                 m_pWatcherSlot{ nullptr };
	sd_bus_slot*                 m_pFollowSlot{ nullptr };
};
} // namespace Klip::Tests
