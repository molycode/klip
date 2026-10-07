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

	// What ItemsPropertiesUpdated said the toggle now reads.
	std::string toggleLabel;
};

// A panel: org.kde.StatusNotifierWatcher, and whatever listens to an item's signals, on a connection and a
// thread of its own. What it records is touched only on that thread, so every accessor goes through Run.
class CFakePanel final : private Tge::SNoCopyNoMove
{
public:

	CFakePanel() = default;
	~CFakePanel() = default;

	bool Initialize();
	void Terminate();

	Bus::CConnection& GetConnection() { return m_connection; }

	// The watcher's name goes and comes back, as on a desktop with no tray.
	void Withdraw();
	bool Restore();

	std::string GetRegistered();

	// Records every signal the service sends from now on.
	void Follow(std::string const& service);
	std::vector<SRecordedSignal> TakeSignals();

	// sd-bus calls these; public only so its plain function pointers can reach them.
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
