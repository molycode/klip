#include "capture/fake_portal.hpp"

#include <algorithm>
#include <format>
#include <string_view>
#include <sys/socket.h>
#include <unistd.h>

namespace Klip::Tests
{
namespace
{
constexpr char const* ScreenCastInterface{ "org.freedesktop.portal.ScreenCast" };
constexpr char const* SessionInterface{ "org.freedesktop.portal.Session" };
constexpr char const* SessionPrefix{ "/org/freedesktop/portal/desktop/session" };

// Both the real portal's capability masks: monitor, window and virtual sources; hidden, embedded and metadata
// cursors.
constexpr uint32_t AllSources{ 7 };
constexpr uint32_t AllCursorModes{ 7 };

CFakePortal* Fake(void* pUserdata)
{
	return static_cast<CFakePortal*>(pUserdata);
}

int GetVersion(sd_bus*, char const*, char const*, char const*, sd_bus_message* pReply, void* pUserdata,
               sd_bus_error*)
{
	return Fake(pUserdata)->GetVersion(pReply);
}

int GetSources(sd_bus*, char const*, char const*, char const*, sd_bus_message* pReply, void*, sd_bus_error*)
{
	return sd_bus_message_append(pReply, "u", AllSources);
}

int GetCursorModes(sd_bus*, char const*, char const*, char const*, sd_bus_message* pReply, void*, sd_bus_error*)
{
	return sd_bus_message_append(pReply, "u", AllCursorModes);
}

int CreateSession(sd_bus_message* pMessage, void* pUserdata, sd_bus_error*)
{
	return Fake(pUserdata)->OnCreateSession(pMessage);
}

int SelectSources(sd_bus_message* pMessage, void* pUserdata, sd_bus_error*)
{
	return Fake(pUserdata)->OnSelectSources(pMessage);
}

int Start(sd_bus_message* pMessage, void* pUserdata, sd_bus_error*)
{
	return Fake(pUserdata)->OnStart(pMessage);
}

int OpenPipeWireRemote(sd_bus_message* pMessage, void* pUserdata, sd_bus_error*)
{
	return Fake(pUserdata)->OnOpenPipeWireRemote(pMessage);
}

int Close(sd_bus_message* pMessage, void* pUserdata, sd_bus_error*)
{
	return Fake(pUserdata)->OnClose(pMessage);
}

sd_bus_vtable const ScreenCastVtable[]{
	SD_BUS_VTABLE_START(0),
	SD_BUS_PROPERTY("version", "u", GetVersion, 0, 0),
	SD_BUS_PROPERTY("AvailableSourceTypes", "u", GetSources, 0, 0),
	SD_BUS_PROPERTY("AvailableCursorModes", "u", GetCursorModes, 0, 0),
	SD_BUS_METHOD("CreateSession", "a{sv}", "o", CreateSession, 0),
	SD_BUS_METHOD("SelectSources", "oa{sv}", "o", SelectSources, 0),
	SD_BUS_METHOD("Start", "osa{sv}", "o", Start, 0),
	SD_BUS_METHOD("OpenPipeWireRemote", "oa{sv}", "h", OpenPipeWireRemote, 0),
	SD_BUS_VTABLE_END
};

sd_bus_vtable const SessionVtable[]{
	SD_BUS_VTABLE_START(0),
	SD_BUS_METHOD("Close", "", "", Close, 0),
	SD_BUS_VTABLE_END
};

// As the portal forms it: the caller's unique name without its colon, dots made underscores.
std::string PathFor(sd_bus_message* pCall, std::string_view prefix, std::string_view token)
{
	std::string sender{ sd_bus_message_get_sender(pCall) };

	sender.erase(0, 1);
	std::ranges::replace(sender, '.', '_');

	return std::format("{}/{}/{}", prefix, sender, token);
}

std::string ReadToken(sd_bus_message* pMessage, std::string_view wanted)
{
	std::string token;

	Bus::ReadDict(pMessage, [&token, wanted](std::string_view key, sd_bus_message* pEntry) {
		return key == wanted && Bus::ReadString(pEntry, token);
	});

	return token;
}
} // namespace

//////////////////////////////////////////////////////////////////////////
bool CFakePortal::Initialize()
{
	bool initialized{ false };

	if (m_connection.Initialize("fake-portal"))
	{
		m_connection.Run([this, &initialized](sd_bus* pBus) {
			initialized = sd_bus_add_object_vtable(pBus, &m_pScreenCastSlot, Bus::PortalPath, ScreenCastInterface,
			                                       ScreenCastVtable, this) >= 0
			              && sd_bus_add_fallback_vtable(pBus, &m_pSessionSlot, SessionPrefix, SessionInterface,
			                                            SessionVtable, nullptr, this) >= 0
			              && sd_bus_request_name(pBus, Bus::PortalService, 0) >= 0;
		});
	}

	return initialized;
}

//////////////////////////////////////////////////////////////////////////
void CFakePortal::Terminate()
{
	m_connection.Run([this](sd_bus* pBus) {
		sd_bus_release_name(pBus, Bus::PortalService);

		m_pScreenCastSlot = sd_bus_slot_unref(m_pScreenCastSlot);
		m_pSessionSlot = sd_bus_slot_unref(m_pSessionSlot);

		if (m_record.remotePeer >= 0)
		{
			close(m_record.remotePeer);
		}
	});

	m_connection.Terminate();
}

//////////////////////////////////////////////////////////////////////////
void CFakePortal::Configure(SFakeScript const& script)
{
	m_connection.Run([this, &script](sd_bus*) {
		if (m_record.remotePeer >= 0)
		{
			close(m_record.remotePeer);
		}

		m_script = script;
		m_record = SFakeRecord{};
		m_heldRequestPath.clear();
		m_releaseOnArrival = false;
	});
}

//////////////////////////////////////////////////////////////////////////
SFakeRecord CFakePortal::GetRecord()
{
	SFakeRecord record;

	m_connection.Run([this, &record](sd_bus*) { record = m_record; });

	return record;
}

//////////////////////////////////////////////////////////////////////////
void CFakePortal::ReleaseStart()
{
	m_connection.Run([this](sd_bus* pBus) {
		if (m_heldRequestPath.empty())
		{
			m_releaseOnArrival = true;
		}
		else
		{
			SendStartResponse(pBus);
		}
	});
}

//////////////////////////////////////////////////////////////////////////
void CFakePortal::CloseFromCompositor()
{
	m_connection.Run([this](sd_bus* pBus) {
		sd_bus_emit_signal(pBus, m_sessionPath.c_str(), SessionInterface, "Closed", "a{sv}", 0);
	});
}

//////////////////////////////////////////////////////////////////////////
void CFakePortal::Withdraw()
{
	m_connection.Run([](sd_bus* pBus) { sd_bus_release_name(pBus, Bus::PortalService); });
}

//////////////////////////////////////////////////////////////////////////
bool CFakePortal::Restore()
{
	bool restored{ false };

	m_connection.Run([&restored](sd_bus* pBus) {
		restored = sd_bus_request_name(pBus, Bus::PortalService, 0) >= 0;
	});

	return restored;
}

//////////////////////////////////////////////////////////////////////////
int CFakePortal::GetVersion(sd_bus_message* pReply)
{
	return sd_bus_message_append(pReply, "u", m_script.version);
}

//////////////////////////////////////////////////////////////////////////
int CFakePortal::OnCreateSession(sd_bus_message* pMessage)
{
	std::string requestToken;
	std::string sessionToken;

	Bus::ReadDict(pMessage, [&requestToken, &sessionToken](std::string_view key, sd_bus_message* pEntry) {
		return (key == "handle_token" && Bus::ReadString(pEntry, requestToken))
		       || (key == "session_handle_token" && Bus::ReadString(pEntry, sessionToken));
	});

	m_sessionPath = PathFor(pMessage, SessionPrefix, sessionToken);

	return Answer(pMessage, requestToken, m_script.createSessionResponse, { { "session_handle", m_sessionPath } });
}

//////////////////////////////////////////////////////////////////////////
int CFakePortal::OnSelectSources(sd_bus_message* pMessage)
{
	char const* pSession{ nullptr };
	std::string requestToken;

	m_record.restoreToken.clear();

	int result{ sd_bus_message_read(pMessage, "o", &pSession) };

	if (result >= 0)
	{
		result = Bus::ReadDict(pMessage, [this, &requestToken](std::string_view key, sd_bus_message* pEntry) {
			bool read{ false };

			if (key == "handle_token" || key == "restore_token")
			{
				read = Bus::ReadString(pEntry, key == "handle_token" ? requestToken : m_record.restoreToken);
			}
			else if (key == "types" || key == "persist_mode")
			{
				read = sd_bus_message_read(pEntry, "v", "u",
				                           key == "types" ? &m_record.types : &m_record.persistMode) >= 0;
			}

			return read;
		});
	}

	return result < 0 ? result : Answer(pMessage, requestToken, m_script.selectSourcesResponse, {});
}

//////////////////////////////////////////////////////////////////////////
int CFakePortal::OnStart(sd_bus_message* pMessage)
{
	char const* pSession{ nullptr };
	char const* pParent{ nullptr };
	int result{ sd_bus_message_read(pMessage, "os", &pSession, &pParent) };

	if (result >= 0)
	{
		m_heldRequestPath = PathFor(pMessage, std::format("{}/request", Bus::PortalPath),
		                            ReadToken(pMessage, "handle_token"));
		result = sd_bus_reply_method_return(pMessage, "o", m_heldRequestPath.c_str());
	}

	if (result >= 0 && (!m_script.holdStart || m_releaseOnArrival))
	{
		result = SendStartResponse(sd_bus_message_get_bus(pMessage));
	}

	return result;
}

//////////////////////////////////////////////////////////////////////////
int CFakePortal::SendStartResponse(sd_bus* pBus)
{
	sd_bus_message* pSignal{ nullptr };
	int result{ sd_bus_message_new_signal(pBus, &pSignal, m_heldRequestPath.c_str(), Bus::RequestInterface,
	                                      "Response") };

	if (result >= 0)
	{
		result = sd_bus_message_append(pSignal, "u", m_script.startResponse);
	}

	if (result >= 0)
	{
		result = sd_bus_message_open_container(pSignal, 'a', "{sv}");
	}

	if (result >= 0 && !m_script.restoreToken.empty())
	{
		result = sd_bus_message_append(pSignal, "{sv}", "restore_token", "s", m_script.restoreToken.c_str());
	}

	if (result >= 0)
	{
		result = sd_bus_message_open_container(pSignal, 'e', "sv");
	}

	if (result >= 0)
	{
		result = sd_bus_message_append(pSignal, "s", "streams");
	}

	if (result >= 0)
	{
		result = sd_bus_message_open_container(pSignal, 'v', "a(ua{sv})");
	}

	if (result >= 0)
	{
		result = sd_bus_message_open_container(pSignal, 'a', "(ua{sv})");
	}

	for (SFakeStream const& stream : m_script.streams)
	{
		if (result >= 0)
		{
			result = stream.hasSize
			             ? sd_bus_message_append(pSignal, "(ua{sv})", stream.nodeId, 1, "size", "(ii)", stream.width,
			                                     stream.height)
			             : sd_bus_message_append(pSignal, "(ua{sv})", stream.nodeId, 0);
		}
	}

	for (uint32_t i{ 0 }; i < 4 && result >= 0; ++i)
	{
		result = sd_bus_message_close_container(pSignal);
	}

	if (result >= 0)
	{
		result = sd_bus_send(pBus, pSignal, nullptr);
	}

	sd_bus_message_unref(pSignal);
	m_heldRequestPath.clear();
	m_releaseOnArrival = false;

	return result;
}

//////////////////////////////////////////////////////////////////////////
int CFakePortal::OnOpenPipeWireRemote(sd_bus_message* pMessage)
{
	int ends[2]{ -1, -1 };
	int result{ socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, ends) };

	if (result >= 0)
	{
		// sd-bus sends a duplicate, so this end is closed here either way.
		result = sd_bus_reply_method_return(pMessage, "h", ends[0]);
		close(ends[0]);

		if (m_record.remotePeer >= 0)
		{
			close(m_record.remotePeer);
		}

		m_record.remotePeer = ends[1];
	}

	return result;
}

//////////////////////////////////////////////////////////////////////////
int CFakePortal::OnClose(sd_bus_message* pMessage)
{
	++m_record.numCloses;

	int result{ sd_bus_reply_method_return(pMessage, "") };

	// The real portal does not send Closed for a session its owner closed; this one does, so that Klip ignoring
	// its own Close is something a test can see.
	if (result >= 0)
	{
		result = sd_bus_emit_signal(sd_bus_message_get_bus(pMessage), m_sessionPath.c_str(), SessionInterface,
		                            "Closed", "a{sv}", 0);
	}

	return result;
}

//////////////////////////////////////////////////////////////////////////
int CFakePortal::Answer(sd_bus_message* pCall, std::string const& token, uint32_t response,
                        std::vector<Bus::SOption> const& results)
{
	sd_bus* const pBus{ sd_bus_message_get_bus(pCall) };
	std::string const path{ PathFor(pCall, std::format("{}/request", Bus::PortalPath), token) };
	sd_bus_message* pSignal{ nullptr };

	int result{ sd_bus_reply_method_return(pCall, "o", path.c_str()) };

	if (result >= 0)
	{
		result = sd_bus_message_new_signal(pBus, &pSignal, path.c_str(), Bus::RequestInterface, "Response");
	}

	if (result >= 0)
	{
		result = sd_bus_message_append(pSignal, "u", response);
	}

	if (result >= 0)
	{
		result = Bus::AppendOptions(pSignal, results);
	}

	if (result >= 0)
	{
		result = sd_bus_send(pBus, pSignal, nullptr);
	}

	sd_bus_message_unref(pSignal);

	return result;
}
} // namespace Klip::Tests
