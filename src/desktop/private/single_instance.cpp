#include "desktop/single_instance.hpp"

#include "bus/connection.hpp"
#include "bus/portal.hpp"
#include "log.hpp"

#include <systemd/sd-bus.h>

#include <cerrno>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace Klip::Desktop
{
CSingleInstance gSingleInstance;

namespace
{
constexpr char const* Name{ "io.github.molycode.Klip" };

// The freedesktop convention: the bus name with its dots made slashes.
constexpr char const* ObjectPath{ "/io/github/molycode/Klip" };
constexpr char const* ApplicationInterface{ "org.freedesktop.Application" };
constexpr char const* ActivationTokenKey{ "activation-token" };
} // namespace

//////////////////////////////////////////////////////////////////////////
sd_bus_vtable const* CSingleInstance::GetVtable()
{
	static sd_bus_vtable const vtable[]{
		SD_BUS_VTABLE_START(0),
		SD_BUS_METHOD("Activate", "a{sv}", "",
		              [](sd_bus_message* pCall, void* pInstance, sd_bus_error*) {
			              return static_cast<CSingleInstance*>(pInstance)->OnActivate(pCall);
		              },
		              0),
		SD_BUS_VTABLE_END
	};

	return vtable;
}

//////////////////////////////////////////////////////////////////////////
bool CSingleInstance::Claim()
{
	bool claimed{ true };

	Bus::gConnection.Run([this, &claimed](sd_bus* pBus) {
		int const result{ sd_bus_request_name(pBus, Name, 0) };

		if (result == -EEXIST)
		{
			claimed = false;
		}
		else if (result < 0)
		{
			gLog.Warning("Could not claim {}: {}", Name, std::generic_category().message(-result));
		}
		else
		{
			m_owned = true;
		}
	});

	return claimed;
}

//////////////////////////////////////////////////////////////////////////
void CSingleInstance::AskOwnerToShow(std::string_view activationToken)
{
	std::vector<Bus::SOption> platformData;

	if (!activationToken.empty())
	{
		platformData.push_back({ ActivationTokenKey, std::string{ activationToken } });
	}

	Bus::gConnection.Run([&platformData](sd_bus* pBus) {
		sd_bus_error error{ SD_BUS_ERROR_NULL };
		sd_bus_message* pCall{ nullptr };
		int result{ sd_bus_message_new_method_call(pBus, &pCall, Name, ObjectPath, ApplicationInterface, "Activate") };

		if (result >= 0)
		{
			result = Bus::AppendOptions(pCall, platformData);
		}

		if (result >= 0)
		{
			result = sd_bus_call(pBus, pCall, 0, &error, nullptr);
		}

		if (result < 0)
		{
			gLog.Warning("The running Klip did not answer: {}", Bus::Describe(error, result));
		}

		sd_bus_message_unref(pCall);
		sd_bus_error_free(&error);
	});
}

//////////////////////////////////////////////////////////////////////////
void CSingleInstance::Serve(RequestCallback onRequest)
{
	Bus::gConnection.Run([this, &onRequest](sd_bus* pBus) {
		m_onRequest = std::move(onRequest);

		int const result{ sd_bus_add_object_vtable(pBus, &m_pSlot, ObjectPath, ApplicationInterface, GetVtable(),
		                                           this) };

		if (result < 0)
		{
			gLog.Warning("A second Klip will not be able to raise this one: {}",
			             std::generic_category().message(-result));
		}
	});
}

//////////////////////////////////////////////////////////////////////////
void CSingleInstance::Terminate()
{
	Bus::gConnection.Run([this](sd_bus* pBus) {
		m_pSlot = sd_bus_slot_unref(m_pSlot);

		if (m_owned)
		{
			sd_bus_release_name(pBus, Name);
			m_owned = false;
		}

		m_onRequest = nullptr;
	});
}

//////////////////////////////////////////////////////////////////////////
int CSingleInstance::OnActivate(sd_bus_message* pCall)
{
	std::string token;
	int const result{ Bus::ReadDict(pCall, [&token](std::string_view key, sd_bus_message* pEntry) {
		return key == ActivationTokenKey && Bus::ReadString(pEntry, token);
	}) };

	if (result >= 0 && m_onRequest)
	{
		if (!token.empty())
		{
			m_onRequest(SRequest{ ERequest::ActivationToken, token });
		}

		m_onRequest(SRequest{ ERequest::Show, {} });
	}

	return result < 0 ? result : sd_bus_reply_method_return(pCall, "");
}
} // namespace Klip::Desktop
