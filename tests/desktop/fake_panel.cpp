#include "desktop/fake_panel.hpp"

#include "bus/portal.hpp"

#include <string_view>
#include <utility>

namespace Klip::Tests
{
namespace
{
constexpr char const* WatcherService{ "org.kde.StatusNotifierWatcher" };
constexpr char const* WatcherPath{ "/StatusNotifierWatcher" };

int Register(sd_bus_message* pCall, void* pPanel, sd_bus_error*)
{
	return static_cast<CFakePanel*>(pPanel)->OnRegister(pCall);
}

int Record(sd_bus_message* pMessage, void* pPanel, sd_bus_error*)
{
	return static_cast<CFakePanel*>(pPanel)->OnSignal(pMessage);
}

sd_bus_vtable const WatcherVtable[]{
	SD_BUS_VTABLE_START(0),
	SD_BUS_METHOD("RegisterStatusNotifierItem", "s", "", Register, 0),
	SD_BUS_VTABLE_END
};

// a(ia{sv}): the label of the one item it names.
std::string ReadUpdatedLabel(sd_bus_message* pMessage)
{
	std::string label;

	if (sd_bus_message_enter_container(pMessage, 'a', "(ia{sv})") > 0
	    && sd_bus_message_enter_container(pMessage, 'r', "ia{sv}") > 0
	    && sd_bus_message_skip(pMessage, "i") >= 0)
	{
		Bus::ReadDict(pMessage, [&label](std::string_view key, sd_bus_message* pEntry) {
			return key == "label" && Bus::ReadString(pEntry, label);
		});
	}

	return label;
}
} // namespace

//////////////////////////////////////////////////////////////////////////
bool CFakePanel::Initialize()
{
	bool initialized{ false };

	if (m_connection.Initialize("fake-panel"))
	{
		m_connection.Run([this, &initialized](sd_bus* pBus) {
			initialized = sd_bus_add_object_vtable(pBus, &m_pWatcherSlot, WatcherPath, WatcherService,
			                                       WatcherVtable, this) >= 0
			              && sd_bus_request_name(pBus, WatcherService, 0) >= 0;
		});
	}

	return initialized;
}

//////////////////////////////////////////////////////////////////////////
void CFakePanel::Terminate()
{
	m_connection.Run([this](sd_bus* pBus) {
		sd_bus_release_name(pBus, WatcherService);

		m_pWatcherSlot = sd_bus_slot_unref(m_pWatcherSlot);
		m_pFollowSlot = sd_bus_slot_unref(m_pFollowSlot);
	});

	m_connection.Terminate();
}

//////////////////////////////////////////////////////////////////////////
void CFakePanel::Withdraw()
{
	m_connection.Run([](sd_bus* pBus) { sd_bus_release_name(pBus, WatcherService); });
}

//////////////////////////////////////////////////////////////////////////
bool CFakePanel::Restore()
{
	bool restored{ false };

	m_connection.Run([&restored](sd_bus* pBus) { restored = sd_bus_request_name(pBus, WatcherService, 0) >= 0; });

	return restored;
}

//////////////////////////////////////////////////////////////////////////
std::string CFakePanel::GetRegistered()
{
	std::string registered;

	m_connection.Run([this, &registered](sd_bus*) { registered = m_registered; });

	return registered;
}

//////////////////////////////////////////////////////////////////////////
void CFakePanel::Follow(std::string const& service)
{
	m_connection.Run([this, &service](sd_bus* pBus) {
		m_pFollowSlot = sd_bus_slot_unref(m_pFollowSlot);
		m_signals.clear();

		sd_bus_match_signal(pBus, &m_pFollowSlot, service.c_str(), nullptr, nullptr, nullptr, Record, this);
	});
}

//////////////////////////////////////////////////////////////////////////
std::vector<SRecordedSignal> CFakePanel::TakeSignals()
{
	std::vector<SRecordedSignal> taken;

	m_connection.Run([this, &taken](sd_bus*) { taken.swap(m_signals); });

	return taken;
}

//////////////////////////////////////////////////////////////////////////
int CFakePanel::OnRegister(sd_bus_message* pCall)
{
	char const* pService{ nullptr };
	int result{ sd_bus_message_read(pCall, "s", &pService) };

	if (result >= 0)
	{
		m_registered = pService;
		result = sd_bus_reply_method_return(pCall, "");
	}

	return result;
}

//////////////////////////////////////////////////////////////////////////
int CFakePanel::OnSignal(sd_bus_message* pMessage)
{
	SRecordedSignal recorded;
	recorded.member = sd_bus_message_get_member(pMessage);
	recorded.signature = sd_bus_message_get_signature(pMessage, 1);

	if (recorded.member == "ItemsPropertiesUpdated")
	{
		recorded.toggleLabel = ReadUpdatedLabel(pMessage);
	}

	m_signals.push_back(std::move(recorded));

	return 0;
}
} // namespace Klip::Tests
