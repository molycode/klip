#include "capture/portal_session.hpp"

#include "bus/connection.hpp"
#include "bus/portal.hpp"
#include "log.hpp"

#include <systemd/sd-bus.h>
#include <tge/profiling/profiling.hpp>

#include <fcntl.h>
#include <vector>

namespace Klip::Capture
{
namespace
{
constexpr char const* ScreenCastInterface{ "org.freedesktop.portal.ScreenCast" };
constexpr char const* SessionInterface{ "org.freedesktop.portal.Session" };

// Fixed by the ScreenCast portal specification.
constexpr uint32_t SourceTypeMonitor{ 1 };
constexpr uint32_t SourceTypeWindow{ 2 };
constexpr uint32_t CursorModeEmbedded{ 2 };
constexpr uint32_t DoNotPersist{ 0 };
constexpr uint32_t PersistUntilRevoked{ 2 };

// persist_mode and restore_token arrived in 4.
constexpr uint32_t MinimumPortalVersion{ 4 };

struct SPortalStream final
{
	uint32_t nodeId{ 0 };
	int32_t  width{ 0 };
	int32_t  height{ 0 };
};

uint32_t ReadPortalProperty(sd_bus* pBus, char const* pName)
{
	sd_bus_error error{ SD_BUS_ERROR_NULL };
	uint32_t value{ 0 };

	if (sd_bus_get_property_trivial(pBus, Bus::PortalService, Bus::PortalPath, ScreenCastInterface, pName, &error,
	                                'u', &value) < 0)
	{
		value = 0;
	}

	sd_bus_error_free(&error);

	return value;
}

// a(ua{sv}), where each dictionary may carry a (ii) size.
int ReadStreams(sd_bus_message* pMessage, std::vector<SPortalStream>& streams)
{
	int result{ sd_bus_message_enter_container(pMessage, 'v', "a(ua{sv})") };

	if (result >= 0)
	{
		result = sd_bus_message_enter_container(pMessage, 'a', "(ua{sv})");
	}

	int entered{ result };

	while (result >= 0 && entered > 0)
	{
		entered = sd_bus_message_enter_container(pMessage, 'r', "ua{sv}");

		if (entered > 0)
		{
			SPortalStream stream;
			result = sd_bus_message_read(pMessage, "u", &stream.nodeId);

			if (result >= 0)
			{
				result = Bus::ReadDict(pMessage, [&stream](std::string_view key, sd_bus_message* pEntry) {
					return key == "size" && sd_bus_message_read(pEntry, "v", "(ii)", &stream.width, &stream.height) >= 0;
				});
			}

			if (result >= 0)
			{
				result = sd_bus_message_exit_container(pMessage);
				streams.push_back(stream);
			}
		}
		else
		{
			result = entered;
		}
	}

	if (result >= 0)
	{
		result = sd_bus_message_exit_container(pMessage);
	}

	if (result >= 0)
	{
		result = sd_bus_message_exit_container(pMessage);
	}

	return result;
}
} // namespace

//////////////////////////////////////////////////////////////////////////
bool KeepsGrant(ESourceType source, bool rememberWindow)
{
	return source == ESourceType::Screen || rememberWindow;
}

//////////////////////////////////////////////////////////////////////////
bool CPortalSession::Initialize(ClosedCallback onClosed)
{
	TGE_PROFILE_SCOPE_N("Portal: initialize");

	m_onClosed = std::move(onClosed);

	Bus::gConnection.Run([this](sd_bus* pBus) { Probe(pBus); });

	return m_initialized;
}

//////////////////////////////////////////////////////////////////////////
void CPortalSession::Probe(sd_bus* pBus)
{
	uint32_t const version{ ReadPortalProperty(pBus, "version") };

	if (version == 0)
	{
		gLog.Error("No ScreenCast portal on the session bus. Install xdg-desktop-portal and a backend for "
		           "your desktop (xdg-desktop-portal-gnome, -kde or -wlr).");
	}
	else if (version < MinimumPortalVersion)
	{
		gLog.Error("ScreenCast portal is version {}; Klip needs {} for persist_mode.", version,
		           MinimumPortalVersion);
	}
	else
	{
		m_availableSourceTypes = ReadPortalProperty(pBus, "AvailableSourceTypes");
		m_availableCursorModes = ReadPortalProperty(pBus, "AvailableCursorModes");

		if ((m_availableSourceTypes & SourceTypeMonitor) == 0)
		{
			gLog.Error("The ScreenCast portal offers no monitor source.");
		}
		else
		{
			gLog.Info("ScreenCast portal v{}, sources 0x{:x}, cursor modes 0x{:x}", version,
			          m_availableSourceTypes, m_availableCursorModes);

			m_initialized = true;
		}
	}
}

//////////////////////////////////////////////////////////////////////////
void CPortalSession::Terminate()
{
	Bus::gConnection.Run([this](sd_bus* pBus) {
		CloseSession(pBus);

		m_pResponseSlot = sd_bus_slot_unref(m_pResponseSlot);
		m_callback = nullptr;
		m_onClosed = nullptr;
		m_step = EStep::None;
		m_initialized = false;
		m_busy = false;
	});
}

//////////////////////////////////////////////////////////////////////////
void CPortalSession::Close()
{
	Bus::gConnection.Post([this](sd_bus* pBus) { CloseSession(pBus); });
}

//////////////////////////////////////////////////////////////////////////
void CPortalSession::CloseSession(sd_bus* pBus)
{
	TGE_PROFILE_SCOPE_N("Portal: close");

	if (!m_sessionHandle.empty())
	{
		// Unsubscribed before the call, and left so: the Closed that may follow is Klip's own doing.
		m_sessionLive = false;
		m_pClosedSlot = sd_bus_slot_unref(m_pClosedSlot);

		sd_bus_error error{ SD_BUS_ERROR_NULL };
		int const result{ sd_bus_call_method(pBus, Bus::PortalService, m_sessionHandle.c_str(), SessionInterface,
		                                     "Close", &error, nullptr, "") };

		if (result < 0)
		{
			gLog.Warning("Closing the portal session failed: {}", Bus::Describe(error, result));
		}

		sd_bus_error_free(&error);
		m_sessionHandle.clear();
	}
}

//////////////////////////////////////////////////////////////////////////
void CPortalSession::OnSessionClosed()
{
	if (m_sessionLive)
	{
		m_sessionLive = false;

		gLog.Warning("The compositor ended the screen cast.");

		m_pClosedSlot = sd_bus_slot_unref(m_pClosedSlot);
		m_sessionHandle.clear();

		if (m_onClosed)
		{
			m_onClosed();
		}
	}
}

//////////////////////////////////////////////////////////////////////////
void CPortalSession::Start(ESourceType source, bool rememberWindow, std::string restoreToken,
                           ResultCallback callback)
{
	Bus::gConnection.Post([this, source, rememberWindow, restoreToken = std::move(restoreToken),
	                       callback = std::move(callback)](sd_bus* pBus) mutable {
		if (!m_initialized || m_busy)
		{
			// Directly rather than through Finish, which would close the session another Start has open.
			gLog.Error("Start called on a portal session that is {}.", m_busy ? "already running" : "not ready");
			callback(SPortalGrant{});
		}
		else
		{
			m_source = source;
			m_rememberWindow = rememberWindow;
			m_restoreToken = std::move(restoreToken);

			Begin(pBus, std::move(callback));
		}
	});
}

//////////////////////////////////////////////////////////////////////////
void CPortalSession::Begin(sd_bus* pBus, ResultCallback callback)
{
	TGE_PROFILE_SCOPE_N("Portal: create session");

	m_callback = std::move(callback);
	m_busy = true;

	bool const sent{ Request(pBus, EStep::CreateSession, "CreateSession",
	                         [](sd_bus_message* pCall, std::string const& token) {
		                         return Bus::AppendOptions(pCall, { { "handle_token", token },
		                                                            { "session_handle_token",
		                                                              Bus::MakeHandleToken() } });
	                         }) };

	if (!sent)
	{
		Finish(pBus, SPortalGrant{});
	}
}

//////////////////////////////////////////////////////////////////////////
bool CPortalSession::Request(sd_bus* pBus, EStep step, char const* pMember,
                             std::function<int(sd_bus_message*, std::string const& token)> const& fill)
{
	std::string const token{ Bus::MakeHandleToken() };
	std::string const path{ Bus::GetRequestPath(pBus, token) };

	// In the member's own scope, so it may reach OnResponse.
	sd_bus_message_handler_t const onResponse{ [](sd_bus_message* pMessage, void* pSession, sd_bus_error*) {
		static_cast<CPortalSession*>(pSession)->OnResponse(pMessage);

		return 0;
	} };

	m_pResponseSlot = sd_bus_slot_unref(m_pResponseSlot);
	m_step = step;

	sd_bus_error error{ SD_BUS_ERROR_NULL };
	sd_bus_message* pCall{ nullptr };
	int result{ sd_bus_match_signal(pBus, &m_pResponseSlot, Bus::PortalService, path.c_str(), Bus::RequestInterface,
	                                "Response", onResponse, this) };

	if (result >= 0)
	{
		result = sd_bus_message_new_method_call(pBus, &pCall, Bus::PortalService, Bus::PortalPath,
		                                        ScreenCastInterface, pMember);
	}

	if (result >= 0)
	{
		result = fill(pCall, token);
	}

	if (result >= 0)
	{
		result = sd_bus_call(pBus, pCall, 0, &error, nullptr);
	}

	if (result < 0)
	{
		gLog.Error("{} failed: {}", pMember, Bus::Describe(error, result));
		m_pResponseSlot = sd_bus_slot_unref(m_pResponseSlot);
		m_step = EStep::None;
	}

	sd_bus_message_unref(pCall);
	sd_bus_error_free(&error);

	return result >= 0;
}

//////////////////////////////////////////////////////////////////////////
void CPortalSession::OnResponse(sd_bus_message* pMessage)
{
	sd_bus* const pBus{ sd_bus_message_get_bus(pMessage) };
	EStep const step{ m_step };
	char const* pMember{ step == EStep::CreateSession ? "CreateSession"
	                     : step == EStep::SelectSources ? "SelectSources"
	                                                    : "Start" };
	uint32_t response{ 0 };

	// Unsubscribed inside its own callback, which sd-bus allows.
	m_pResponseSlot = sd_bus_slot_unref(m_pResponseSlot);
	m_step = EStep::None;

	if (sd_bus_message_read(pMessage, "u", &response) < 0)
	{
		gLog.Error("{} answered with a malformed Response.", pMember);
		Finish(pBus, SPortalGrant{});
	}
	else if (step == EStep::Start && response == Bus::ResponseCancelled)
	{
		SPortalGrant cancelled;
		cancelled.result = EPortalResult::Cancelled;

		gLog.Warning("The screen-cast request was dismissed.");
		Finish(pBus, cancelled);
	}
	else if (response != Bus::ResponseSuccess)
	{
		gLog.Error("{} returned {}.", pMember, response);
		Finish(pBus, SPortalGrant{});
	}
	else if (step == EStep::CreateSession)
	{
		OnSessionCreated(pBus, pMessage);
	}
	else if (step == EStep::SelectSources)
	{
		OnSourcesSelected(pBus);
	}
	else
	{
		OnStarted(pBus, pMessage);
	}
}

//////////////////////////////////////////////////////////////////////////
void CPortalSession::OnSessionCreated(sd_bus* pBus, sd_bus_message* pResults)
{
	TGE_PROFILE_SCOPE_N("Portal: select sources");

	Bus::ReadDict(pResults, [this](std::string_view key, sd_bus_message* pEntry) {
		return key == "session_handle" && Bus::ReadString(pEntry, m_sessionHandle);
	});

	sd_bus_message_handler_t const onClosed{ [](sd_bus_message*, void* pSession, sd_bus_error*) {
		static_cast<CPortalSession*>(pSession)->OnSessionClosed();

		return 0;
	} };

	// The spec's own notice that a cast has ended. The PipeWire stream does not reliably say so.
	int const subscribed{ m_sessionHandle.empty()
	                          ? -1
	                          : sd_bus_match_signal(pBus, &m_pClosedSlot, Bus::PortalService, m_sessionHandle.c_str(),
	                                                SessionInterface, "Closed", onClosed, this) };

	if (subscribed < 0)
	{
		gLog.Error("CreateSession gave no session to follow.");
		Finish(pBus, SPortalGrant{});
	}
	else
	{
		m_sessionLive = true;

		std::vector<Bus::SOption> options{
			{ "types", m_source == ESourceType::Window ? SourceTypeWindow : SourceTypeMonitor },
			{ "multiple", false },
			{ "cursor_mode", CursorModeEmbedded },
			{ "persist_mode", KeepsGrant(m_source, m_rememberWindow) ? PersistUntilRevoked : DoNotPersist },
		};

		if (!m_restoreToken.empty())
		{
			options.push_back({ "restore_token", m_restoreToken });
		}

		bool const sent{ Request(pBus, EStep::SelectSources, "SelectSources",
		                         [this, &options](sd_bus_message* pCall, std::string const& token) {
			                         options.push_back({ "handle_token", token });

			                         int result{ sd_bus_message_append(pCall, "o", m_sessionHandle.c_str()) };

			                         return result < 0 ? result : Bus::AppendOptions(pCall, options);
		                         }) };

		if (!sent)
		{
			Finish(pBus, SPortalGrant{});
		}
	}
}

//////////////////////////////////////////////////////////////////////////
void CPortalSession::OnSourcesSelected(sd_bus* pBus)
{
	TGE_PROFILE_SCOPE_N("Portal: start");

	bool const sent{ Request(pBus, EStep::Start, "Start", [this](sd_bus_message* pCall, std::string const& token) {
		int result{ sd_bus_message_append(pCall, "os", m_sessionHandle.c_str(), "") };

		return result < 0 ? result : Bus::AppendOptions(pCall, { { "handle_token", token } });
	}) };

	if (!sent)
	{
		Finish(pBus, SPortalGrant{});
	}
}

//////////////////////////////////////////////////////////////////////////
void CPortalSession::OnStarted(sd_bus* pBus, sd_bus_message* pResults)
{
	TGE_PROFILE_SCOPE_N("Portal: open remote");

	SPortalGrant grant;
	std::vector<SPortalStream> streams;

	int const read{ Bus::ReadDict(pResults, [&grant, &streams](std::string_view key, sd_bus_message* pEntry) {
		bool consumed{ false };

		if (key == "restore_token")
		{
			consumed = Bus::ReadString(pEntry, grant.restoreToken);
		}
		else if (key == "streams")
		{
			consumed = ReadStreams(pEntry, streams) >= 0;
		}

		return consumed;
	}) };

	if (streams.size() > 1)
	{
		// multiple is already false in SelectSources; some pickers offer a choice of several anyway.
		gLog.Warning("{} sources were shared; Klip records the first.", streams.size());
	}

	if (read < 0 || streams.empty())
	{
		gLog.Error("The portal granted the request but returned no stream.");
		Finish(pBus, grant);
	}
	else if (streams.front().width <= 0 || streams.front().height <= 0)
	{
		gLog.Error("The granted stream carries no usable size.");
		Finish(pBus, grant);
	}
	else
	{
		grant.stream.nodeId = streams.front().nodeId;
		grant.stream.width = static_cast<uint32_t>(streams.front().width);
		grant.stream.height = static_cast<uint32_t>(streams.front().height);

		OpenRemote(pBus, std::move(grant));
	}
}

//////////////////////////////////////////////////////////////////////////
void CPortalSession::OpenRemote(sd_bus* pBus, SPortalGrant grant)
{
	sd_bus_error error{ SD_BUS_ERROR_NULL };
	sd_bus_message* pReply{ nullptr };
	int descriptor{ -1 };

	int result{ sd_bus_call_method(pBus, Bus::PortalService, Bus::PortalPath, ScreenCastInterface,
	                               "OpenPipeWireRemote", &error, &pReply, "oa{sv}", m_sessionHandle.c_str(), 0) };

	if (result >= 0)
	{
		result = sd_bus_message_read(pReply, "h", &descriptor);
	}

	if (result < 0)
	{
		gLog.Error("OpenPipeWireRemote failed: {}", Bus::Describe(error, result));
		Finish(pBus, grant);
	}
	else
	{
		// The reply closes its own copy when it goes.
		grant.pipeWireFd = fcntl(descriptor, F_DUPFD_CLOEXEC, 3);

		if (grant.pipeWireFd < 0)
		{
			gLog.Error("Could not duplicate the PipeWire descriptor.");
			Finish(pBus, grant);
		}
		else
		{
			gLog.Info("Capturing node {} at {}x{}", grant.stream.nodeId, grant.stream.width, grant.stream.height);

			grant.result = EPortalResult::Success;
			Finish(pBus, grant);
		}
	}

	sd_bus_message_unref(pReply);
	sd_bus_error_free(&error);
}

//////////////////////////////////////////////////////////////////////////
void CPortalSession::Finish(sd_bus* pBus, SPortalGrant const& grant)
{
	// A refused or abandoned request leaves a session the portal will close by itself, which would then
	// read as the compositor withdrawing a cast that never started.
	if (grant.result != EPortalResult::Success)
	{
		CloseSession(pBus);
	}

	m_busy = false;

	ResultCallback callback{ std::move(m_callback) };
	m_callback = nullptr;

	if (callback)
	{
		callback(grant);
	}
}
} // namespace Klip::Capture
