#include "desktop/tray.hpp"

#include "bus/connection.hpp"
#include "bus/portal.hpp"
#include "log.hpp"
#include "menu.hpp"

#include <systemd/sd-bus.h>
#include <tge/profiling/profiling.hpp>

#include <format>
#include <string_view>
#include <unistd.h>
#include <utility>

namespace Klip::Desktop
{
namespace
{
constexpr char const* ItemInterface{ "org.kde.StatusNotifierItem" };
constexpr char const* ItemPath{ "/StatusNotifierItem" };
constexpr char const* MenuInterface{ "com.canonical.dbusmenu" };
constexpr char const* MenuPath{ "/MenuBar" };
constexpr char const* WatcherService{ "org.kde.StatusNotifierWatcher" };
constexpr char const* WatcherPath{ "/StatusNotifierWatcher" };

// The widest text the label will ever hold, so the panel reserves room and stops twitching.
constexpr char const* LabelGuide{ "00:00:00 · 999.9 GiB/h" };

constexpr uint32_t MenuVersion{ 3 };
constexpr uint32_t MenuRevision{ 1 };

int GetCategory(sd_bus*, char const*, char const*, char const*, sd_bus_message* pReply, void*, sd_bus_error*)
{
	return sd_bus_message_append(pReply, "s", "ApplicationStatus");
}

int GetId(sd_bus*, char const*, char const*, char const*, sd_bus_message* pReply, void*, sd_bus_error*)
{
	return sd_bus_message_append(pReply, "s", "klip");
}

int GetActive(sd_bus*, char const*, char const*, char const*, sd_bus_message* pReply, void*, sd_bus_error*)
{
	return sd_bus_message_append(pReply, "s", "Active");
}

int GetNoName(sd_bus*, char const*, char const*, char const*, sd_bus_message* pReply, void*, sd_bus_error*)
{
	return sd_bus_message_append(pReply, "s", "");
}

int GetMenuPath(sd_bus*, char const*, char const*, char const*, sd_bus_message* pReply, void*, sd_bus_error*)
{
	return sd_bus_message_append(pReply, "o", MenuPath);
}

int GetFalse(sd_bus*, char const*, char const*, char const*, sd_bus_message* pReply, void*, sd_bus_error*)
{
	return sd_bus_message_append(pReply, "b", 0);
}

int GetLabelGuide(sd_bus*, char const*, char const*, char const*, sd_bus_message* pReply, void*, sd_bus_error*)
{
	return sd_bus_message_append(pReply, "s", LabelGuide);
}

int GetMenuVersion(sd_bus*, char const*, char const*, char const*, sd_bus_message* pReply, void*, sd_bus_error*)
{
	return sd_bus_message_append(pReply, "u", MenuVersion);
}

int GetLeftToRight(sd_bus*, char const*, char const*, char const*, sd_bus_message* pReply, void*, sd_bus_error*)
{
	return sd_bus_message_append(pReply, "s", "ltr");
}

int GetNormal(sd_bus*, char const*, char const*, char const*, sd_bus_message* pReply, void*, sd_bus_error*)
{
	return sd_bus_message_append(pReply, "s", "normal");
}

int GetNoPaths(sd_bus*, char const*, char const*, char const*, sd_bus_message* pReply, void*, sd_bus_error*)
{
	return sd_bus_message_append(pReply, "as", 0);
}

// Every method replies, even one that changes nothing: callers wait for the answer.
int Acknowledge(sd_bus_message* pCall, void*, sd_bus_error*)
{
	return sd_bus_reply_method_return(pCall, "");
}

int DeclineToShow(sd_bus_message* pCall, void*, sd_bus_error*)
{
	return sd_bus_reply_method_return(pCall, "b", 0);
}

int AppendMenuItem(sd_bus_message* pMessage, int32_t id, bool recording)
{
	int result{ sd_bus_message_open_container(pMessage, 'r', "ia{sv}") };

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
		result = sd_bus_message_close_container(pMessage);
	}

	return result;
}
} // namespace

//////////////////////////////////////////////////////////////////////////
// Function-local statics: sd-bus keeps the pointer, and member scope lets their lambdas reach the handlers.
sd_bus_vtable const* CTray::GetItemVtable()
{
	static sd_bus_vtable const vtable[]{
		SD_BUS_VTABLE_START(0),
		SD_BUS_PROPERTY("Category", "s", GetCategory, 0, SD_BUS_VTABLE_PROPERTY_CONST),
		SD_BUS_PROPERTY("Id", "s", GetId, 0, SD_BUS_VTABLE_PROPERTY_CONST),
		SD_BUS_PROPERTY("Title", "s",
		                [](sd_bus*, char const*, char const*, char const*, sd_bus_message* pReply, void* pTray,
		                   sd_bus_error*) {
			                return sd_bus_message_append(pReply, "s", static_cast<CTray*>(pTray)->GetTitle().c_str());
		                },
		                0, 0),
		SD_BUS_PROPERTY("Status", "s", GetActive, 0, SD_BUS_VTABLE_PROPERTY_CONST),
		SD_BUS_PROPERTY("IconName", "s", GetNoName, 0, SD_BUS_VTABLE_PROPERTY_CONST),
		SD_BUS_PROPERTY("IconPixmap", "a(iiay)",
		                [](sd_bus*, char const*, char const*, char const*, sd_bus_message* pReply, void* pTray,
		                   sd_bus_error*) { return static_cast<CTray*>(pTray)->AppendIcon(pReply); },
		                0, 0),
		SD_BUS_PROPERTY("AttentionIconName", "s", GetNoName, 0, SD_BUS_VTABLE_PROPERTY_CONST),
		SD_BUS_PROPERTY("OverlayIconName", "s", GetNoName, 0, SD_BUS_VTABLE_PROPERTY_CONST),
		SD_BUS_PROPERTY("ToolTip", "(sa(iiay)ss)",
		                [](sd_bus*, char const*, char const*, char const*, sd_bus_message* pReply, void* pTray,
		                   sd_bus_error*) { return static_cast<CTray*>(pTray)->AppendToolTip(pReply); },
		                0, 0),
		SD_BUS_PROPERTY("Menu", "o", GetMenuPath, 0, SD_BUS_VTABLE_PROPERTY_CONST),
		SD_BUS_PROPERTY("ItemIsMenu", "b", GetFalse, 0, SD_BUS_VTABLE_PROPERTY_CONST),
		SD_BUS_PROPERTY("XAyatanaLabel", "s",
		                [](sd_bus*, char const*, char const*, char const*, sd_bus_message* pReply, void* pTray,
		                   sd_bus_error*) {
			                return sd_bus_message_append(pReply, "s", static_cast<CTray*>(pTray)->m_label.c_str());
		                },
		                0, 0),
		SD_BUS_PROPERTY("XAyatanaLabelGuide", "s", GetLabelGuide, 0, SD_BUS_VTABLE_PROPERTY_CONST),
		SD_BUS_METHOD("Activate", "ii", "",
		              [](sd_bus_message* pCall, void* pTray, sd_bus_error*) {
			              return static_cast<CTray*>(pTray)->OnRequest(pCall, ERequest::Show);
		              },
		              0),
		SD_BUS_METHOD("SecondaryActivate", "ii", "",
		              [](sd_bus_message* pCall, void* pTray, sd_bus_error*) {
			              return static_cast<CTray*>(pTray)->OnRequest(pCall, ERequest::Toggle);
		              },
		              0),
		SD_BUS_METHOD("ContextMenu", "ii", "", Acknowledge, 0),
		SD_BUS_METHOD("Scroll", "is", "", Acknowledge, 0),
		SD_BUS_METHOD("ProvideXdgActivationToken", "s", "",
		              [](sd_bus_message* pCall, void* pTray, sd_bus_error*) {
			              return static_cast<CTray*>(pTray)->OnProvideActivationToken(pCall);
		              },
		              0),
		SD_BUS_SIGNAL("NewIcon", "", 0),
		SD_BUS_SIGNAL("NewTitle", "", 0),
		SD_BUS_SIGNAL("NewToolTip", "", 0),
		SD_BUS_SIGNAL("NewStatus", "s", 0),
		SD_BUS_SIGNAL("XAyatanaNewLabel", "ss", 0),
		SD_BUS_VTABLE_END
	};

	return vtable;
}

//////////////////////////////////////////////////////////////////////////
sd_bus_vtable const* CTray::GetMenuVtable()
{
	static sd_bus_vtable const vtable[]{
		SD_BUS_VTABLE_START(0),
		SD_BUS_PROPERTY("Version", "u", GetMenuVersion, 0, SD_BUS_VTABLE_PROPERTY_CONST),
		SD_BUS_PROPERTY("TextDirection", "s", GetLeftToRight, 0, SD_BUS_VTABLE_PROPERTY_CONST),
		SD_BUS_PROPERTY("Status", "s", GetNormal, 0, SD_BUS_VTABLE_PROPERTY_CONST),
		SD_BUS_PROPERTY("IconThemePath", "as", GetNoPaths, 0, SD_BUS_VTABLE_PROPERTY_CONST),
		SD_BUS_METHOD("GetLayout", "iias", "u(ia{sv}av)",
		              [](sd_bus_message* pCall, void* pTray, sd_bus_error*) {
			              return static_cast<CTray*>(pTray)->OnGetLayout(pCall);
		              },
		              0),
		SD_BUS_METHOD("GetGroupProperties", "aias", "a(ia{sv})",
		              [](sd_bus_message* pCall, void* pTray, sd_bus_error*) {
			              return static_cast<CTray*>(pTray)->OnGetGroupProperties(pCall);
		              },
		              0),
		SD_BUS_METHOD("GetProperty", "is", "v",
		              [](sd_bus_message* pCall, void* pTray, sd_bus_error*) {
			              return static_cast<CTray*>(pTray)->OnGetProperty(pCall);
		              },
		              0),
		SD_BUS_METHOD("Event", "isvu", "",
		              [](sd_bus_message* pCall, void* pTray, sd_bus_error*) {
			              return static_cast<CTray*>(pTray)->OnEvent(pCall);
		              },
		              SD_BUS_VTABLE_METHOD_NO_REPLY),
		SD_BUS_METHOD("AboutToShow", "i", "b", DeclineToShow, 0),
		SD_BUS_SIGNAL("ItemsPropertiesUpdated", "a(ia{sv})a(ias)", 0),
		SD_BUS_SIGNAL("LayoutUpdated", "ui", 0),
		SD_BUS_SIGNAL("ItemActivationRequested", "iu", 0),
		SD_BUS_VTABLE_END
	};

	return vtable;
}

//////////////////////////////////////////////////////////////////////////
// By hand rather than through a toolkit's tray: only this interface carries XAyatanaLabel, the text a panel
// shows beside the icon.
bool CTray::Initialize(STrayIcons icons, RequestCallback onRequest)
{
	TGE_PROFILE_SCOPE_N("Startup: tray");

	Bus::gConnection.Run([this, &icons, &onRequest](sd_bus* pBus) {
		m_icons = std::move(icons);
		m_onRequest = std::move(onRequest);

		if (Publish(pBus))
		{
			sd_bus_error error{ SD_BUS_ERROR_NULL };
			int const result{ sd_bus_call_method(pBus, WatcherService, WatcherPath, WatcherService,
			                                     "RegisterStatusNotifierItem", &error, nullptr, "s",
			                                     m_serviceName.c_str()) };

			if (result >= 0)
			{
				// The only member the owner's thread reads, once Initialize's wait returns.
				m_available = true;
			}
			else if (sd_bus_error_has_name(&error, SD_BUS_ERROR_SERVICE_UNKNOWN)
			         || sd_bus_error_has_name(&error, SD_BUS_ERROR_NAME_HAS_NO_OWNER))
			{
				gLog.Warning("No system tray: Klip will stay on screen while recording, and so appear in it.");
			}
			else
			{
				gLog.Warning("The tray refused Klip's item: {}", Bus::Describe(error, result));
			}

			sd_bus_error_free(&error);
		}
	});

	return m_available;
}

//////////////////////////////////////////////////////////////////////////
bool CTray::Publish(sd_bus* pBus)
{
	std::string const name{ std::format("org.kde.StatusNotifierItem-{}-1", getpid()) };
	bool published{ false };

	if (sd_bus_request_name(pBus, name.c_str(), 0) < 0)
	{
		gLog.Warning("Could not take the tray service name.");
	}
	else
	{
		m_serviceName = name;

		if (sd_bus_add_object_vtable(pBus, &m_pItemSlot, ItemPath, ItemInterface, GetItemVtable(), this) < 0)
		{
			gLog.Warning("Could not publish the tray item.");
		}
		else if (sd_bus_add_object_vtable(pBus, &m_pMenuSlot, MenuPath, MenuInterface, GetMenuVtable(), this) < 0)
		{
			gLog.Warning("Could not publish the tray menu.");
		}
		else
		{
			published = true;
		}
	}

	return published;
}

//////////////////////////////////////////////////////////////////////////
void CTray::Terminate()
{
	Bus::gConnection.Run([this](sd_bus* pBus) {
		m_pItemSlot = sd_bus_slot_unref(m_pItemSlot);
		m_pMenuSlot = sd_bus_slot_unref(m_pMenuSlot);

		// Releasing the name is what takes the icon off the panel.
		if (!m_serviceName.empty())
		{
			sd_bus_release_name(pBus, m_serviceName.c_str());
			m_serviceName.clear();
		}

		m_onRequest = nullptr;
		m_label = "Klip";
		m_detail.clear();
		m_recording = false;
		m_available = false;
	});
}

//////////////////////////////////////////////////////////////////////////
void CTray::SetRecording(bool recording)
{
	Bus::gConnection.Post([this, recording](sd_bus* pBus) { ApplyRecording(pBus, recording); });
}

//////////////////////////////////////////////////////////////////////////
void CTray::SetLabel(std::string label)
{
	Bus::gConnection.Post([this, label = std::move(label)](sd_bus* pBus) mutable {
		ApplyLabel(pBus, std::move(label));
	});
}

//////////////////////////////////////////////////////////////////////////
void CTray::SetDetail(std::string detail)
{
	Bus::gConnection.Post([this, detail = std::move(detail)](sd_bus* pBus) mutable {
		ApplyDetail(pBus, std::move(detail));
	});
}

//////////////////////////////////////////////////////////////////////////
void CTray::ApplyRecording(sd_bus* pBus, bool recording)
{
	if (m_recording != recording)
	{
		m_recording = recording;

		if (m_available)
		{
			Emit(pBus, "NewIcon");
			Emit(pBus, "NewTitle");
			Emit(pBus, "NewToolTip");

			sd_bus_message* pSignal{ nullptr };
			int result{ sd_bus_message_new_signal(pBus, &pSignal, MenuPath, MenuInterface, "ItemsPropertiesUpdated") };

			if (result >= 0)
			{
				result = sd_bus_message_open_container(pSignal, 'a', "(ia{sv})");
			}

			if (result >= 0)
			{
				result = AppendMenuItem(pSignal, ToggleId, m_recording);
			}

			if (result >= 0)
			{
				result = sd_bus_message_close_container(pSignal);
			}

			if (result >= 0)
			{
				result = sd_bus_message_append(pSignal, "a(ias)", 0);
			}

			if (result >= 0)
			{
				result = sd_bus_send(pBus, pSignal, nullptr);
			}

			if (result < 0)
			{
				gLog.Warning("Could not announce the tray menu's change.");
			}

			sd_bus_message_unref(pSignal);
		}

		if (!recording)
		{
			ApplyLabel(pBus, "Klip");
			ApplyDetail(pBus, {});
		}
	}
}

//////////////////////////////////////////////////////////////////////////
void CTray::ApplyLabel(sd_bus* pBus, std::string label)
{
	if (m_label != label)
	{
		m_label = std::move(label);

		if (m_available
		    && sd_bus_emit_signal(pBus, ItemPath, ItemInterface, "XAyatanaNewLabel", "ss", m_label.c_str(), LabelGuide)
		           < 0)
		{
			gLog.Warning("Could not announce the tray label.");
		}
	}
}

//////////////////////////////////////////////////////////////////////////
void CTray::ApplyDetail(sd_bus* pBus, std::string detail)
{
	if (m_detail != detail)
	{
		m_detail = std::move(detail);

		if (m_available)
		{
			Emit(pBus, "NewToolTip");
		}
	}
}

//////////////////////////////////////////////////////////////////////////
void CTray::Emit(sd_bus* pBus, char const* pSignal) const
{
	if (sd_bus_emit_signal(pBus, ItemPath, ItemInterface, pSignal, "") < 0)
	{
		gLog.Warning("Could not announce the tray's {}.", pSignal);
	}
}

//////////////////////////////////////////////////////////////////////////
std::string CTray::GetTitle() const
{
	return m_recording ? "Klip — recording" : "Klip";
}

//////////////////////////////////////////////////////////////////////////
int CTray::AppendIcon(sd_bus_message* pReply) const
{
	STrayImage const& image{ m_recording ? m_icons.recording : m_icons.idle };
	int result{ sd_bus_message_open_container(pReply, 'a', "(iiay)") };

	if (result >= 0)
	{
		result = sd_bus_message_open_container(pReply, 'r', "iiay");
	}

	if (result >= 0)
	{
		result = sd_bus_message_append(pReply, "ii", image.width, image.height);
	}

	if (result >= 0)
	{
		result = sd_bus_message_append_array(pReply, 'y', image.pixels.data(), image.pixels.size());
	}

	if (result >= 0)
	{
		result = sd_bus_message_close_container(pReply);
	}

	if (result >= 0)
	{
		result = sd_bus_message_close_container(pReply);
	}

	return result;
}

//////////////////////////////////////////////////////////////////////////
int CTray::AppendToolTip(sd_bus_message* pReply) const
{
	std::string const title{ GetTitle() };

	return sd_bus_message_append(pReply, "(sa(iiay)ss)", "", 0, title.c_str(), m_detail.c_str());
}

//////////////////////////////////////////////////////////////////////////
int CTray::OnRequest(sd_bus_message* pCall, ERequest kind)
{
	if (m_onRequest)
	{
		m_onRequest(SRequest{ kind, {} });
	}

	return sd_bus_reply_method_return(pCall, "");
}

//////////////////////////////////////////////////////////////////////////
int CTray::OnProvideActivationToken(sd_bus_message* pCall)
{
	char const* pToken{ nullptr };
	int result{ sd_bus_message_read(pCall, "s", &pToken) };

	if (result >= 0 && m_onRequest)
	{
		m_onRequest(SRequest{ ERequest::ActivationToken, pToken });
	}

	return result < 0 ? result : sd_bus_reply_method_return(pCall, "");
}

//////////////////////////////////////////////////////////////////////////
int CTray::OnGetLayout(sd_bus_message* pCall) const
{
	int32_t parentId{ 0 };
	int32_t depth{ 0 };
	sd_bus_message* pReply{ nullptr };
	int result{ sd_bus_message_read(pCall, "ii", &parentId, &depth) };

	if (result >= 0)
	{
		result = sd_bus_message_new_method_return(pCall, &pReply);
	}

	if (result >= 0)
	{
		result = sd_bus_message_append(pReply, "u", MenuRevision);
	}

	if (result >= 0)
	{
		result = AppendMenuLayout(pReply, parentId, depth, m_recording);
	}

	if (result >= 0)
	{
		result = sd_bus_send(nullptr, pReply, nullptr);
	}

	sd_bus_message_unref(pReply);

	return result;
}

//////////////////////////////////////////////////////////////////////////
int CTray::OnGetGroupProperties(sd_bus_message* pCall) const
{
	int32_t const* pIds{ nullptr };
	size_t size{ 0 };
	sd_bus_message* pReply{ nullptr };
	int result{ sd_bus_message_read_array(pCall, 'i', reinterpret_cast<void const**>(&pIds), &size) };
	size_t const numIds{ size / sizeof(int32_t) };

	if (result >= 0)
	{
		result = sd_bus_message_new_method_return(pCall, &pReply);
	}

	if (result >= 0)
	{
		result = sd_bus_message_open_container(pReply, 'a', "(ia{sv})");
	}

	// No ids asks for every item.
	if (numIds == 0)
	{
		for (int32_t const id : MenuItemIds)
		{
			if (result >= 0)
			{
				result = AppendMenuItem(pReply, id, m_recording);
			}
		}
	}

	for (size_t index{ 0 }; index < numIds; ++index)
	{
		if (result >= 0)
		{
			result = AppendMenuItem(pReply, pIds[index], m_recording);
		}
	}

	if (result >= 0)
	{
		result = sd_bus_message_close_container(pReply);
	}

	if (result >= 0)
	{
		result = sd_bus_send(nullptr, pReply, nullptr);
	}

	sd_bus_message_unref(pReply);

	return result;
}

//////////////////////////////////////////////////////////////////////////
int CTray::OnGetProperty(sd_bus_message* pCall) const
{
	int32_t id{ 0 };
	char const* pName{ nullptr };
	Bus::SOption property;
	sd_bus_message* pReply{ nullptr };
	int result{ sd_bus_message_read(pCall, "is", &id, &pName) };

	if (result >= 0 && !FindMenuProperty(id, m_recording, pName, property))
	{
		result = sd_bus_reply_method_errorf(pCall, SD_BUS_ERROR_INVALID_ARGS, "Menu item %d has no %s", id, pName);
	}
	else if (result >= 0)
	{
		result = sd_bus_message_new_method_return(pCall, &pReply);

		if (result >= 0)
		{
			result = Bus::AppendValue(pReply, property);
		}

		if (result >= 0)
		{
			result = sd_bus_send(nullptr, pReply, nullptr);
		}
	}

	sd_bus_message_unref(pReply);

	return result;
}

//////////////////////////////////////////////////////////////////////////
int CTray::OnEvent(sd_bus_message* pCall)
{
	int32_t id{ 0 };
	char const* pEvent{ nullptr };
	int result{ sd_bus_message_read(pCall, "is", &id, &pEvent) };

	if (result >= 0 && std::string_view{ pEvent } == "clicked" && m_onRequest)
	{
		if (id == ToggleId)
		{
			m_onRequest(SRequest{ ERequest::Toggle, {} });
		}
		else if (id == ShowId)
		{
			m_onRequest(SRequest{ ERequest::Show, {} });
		}
		else if (id == QuitId)
		{
			m_onRequest(SRequest{ ERequest::Quit, {} });
		}
	}

	// Marked no-reply for clients that send it that way, but a caller that asks still gets its answer.
	return result < 0 ? result : sd_bus_reply_method_return(pCall, "");
}
} // namespace Klip::Desktop
