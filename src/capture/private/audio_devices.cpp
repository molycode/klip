#include "capture/audio_devices.hpp"

#include "log.hpp"

#include <pipewire/pipewire.h>
#include <tge/profiling/profiling.hpp>

#include <algorithm>
#include <cstring>
#include <utility>

namespace Klip::Capture
{
namespace
{
constexpr int64_t SyncTimeoutNs{ 500000000 };
} // namespace

class CDevicesImpl final
{
public:

	struct SNode final
	{
		uint32_t     id{ 0 };
		SAudioDevice device;
		bool         isSink{ false };
	};

	void HandleGlobal(uint32_t id, char const* pType, spa_dict const* pProps);
	void HandleRemove(uint32_t id);
	void HandleDone(uint32_t id, int seq);

	pw_thread_loop* pLoop{ nullptr };
	pw_context*     pContext{ nullptr };
	pw_core*        pCore{ nullptr };
	pw_registry*    pRegistry{ nullptr };
	spa_hook        registryListener{};
	spa_hook        coreListener{};

	std::vector<SNode> nodes;

	int  pendingSeq{ 0 };
	bool synced{ false };
};

namespace
{
void OnRegistryGlobal(void* pData, uint32_t id, uint32_t, char const* pType, uint32_t,
                      spa_dict const* pProps)
{
	static_cast<CDevicesImpl*>(pData)->HandleGlobal(id, pType, pProps);
}

void OnRegistryGlobalRemove(void* pData, uint32_t id)
{
	static_cast<CDevicesImpl*>(pData)->HandleRemove(id);
}

void OnCoreDone(void* pData, uint32_t id, int seq)
{
	static_cast<CDevicesImpl*>(pData)->HandleDone(id, seq);
}

void OnCoreError(void* pData, uint32_t id, int, int result, char const* pMessage)
{
	CDevicesImpl& impl{ *static_cast<CDevicesImpl*>(pData) };

	gLog.Error("PipeWire refused the device listing on {}: {} ({})", id,
	           pMessage != nullptr ? pMessage : "no reason given", result);

	impl.synced = true;
	pw_thread_loop_signal(impl.pLoop, false);
}

// Every member spelled out: -Wmissing-field-initializers rejects a partial designated initializer.
constexpr pw_registry_events RegistryEvents{
	.version = PW_VERSION_REGISTRY_EVENTS,
	.global = OnRegistryGlobal,
	.global_remove = OnRegistryGlobalRemove,
};

constexpr pw_core_events CoreEvents{
	.version = PW_VERSION_CORE_EVENTS,
	.info = nullptr,
	.done = OnCoreDone,
	.ping = nullptr,
	.error = OnCoreError,
	.remove_id = nullptr,
	.bound_id = nullptr,
	.add_mem = nullptr,
	.remove_mem = nullptr,
	.bound_props = nullptr,
};
} // namespace

//////////////////////////////////////////////////////////////////////////
void CDevicesImpl::HandleGlobal(uint32_t id, char const* pType, spa_dict const* pProps)
{
	if (pType != nullptr && pProps != nullptr && std::strcmp(pType, PW_TYPE_INTERFACE_Node) == 0)
	{
		char const* pClass{ spa_dict_lookup(pProps, PW_KEY_MEDIA_CLASS) };
		char const* pName{ spa_dict_lookup(pProps, PW_KEY_NODE_NAME) };

		if (pClass != nullptr && pName != nullptr)
		{
			bool const isSink{ std::strcmp(pClass, "Audio/Sink") == 0 };
			bool const isSource{ std::strcmp(pClass, "Audio/Source") == 0 };

			if (isSink || isSource)
			{
				char const* pDescription{ spa_dict_lookup(pProps, PW_KEY_NODE_DESCRIPTION) };

				SNode node{};
				node.id = id;
				node.device.nodeName = pName;
				node.device.description = pDescription != nullptr ? pDescription : pName;
				node.device.isMonitor = isSink;
				node.isSink = isSink;

				nodes.push_back(std::move(node));
			}
		}
	}
}

//////////////////////////////////////////////////////////////////////////
void CDevicesImpl::HandleRemove(uint32_t id)
{
	std::erase_if(nodes, [id](SNode const& node) { return node.id == id; });
}

//////////////////////////////////////////////////////////////////////////
void CDevicesImpl::HandleDone(uint32_t id, int seq)
{
	if (id == PW_ID_CORE && seq == pendingSeq)
	{
		synced = true;
		pw_thread_loop_signal(pLoop, false);
	}
}

//////////////////////////////////////////////////////////////////////////
bool CAudioDevices::Initialize()
{
	m_pImpl = new CDevicesImpl();
	m_pImpl->pLoop = pw_thread_loop_new("klip-devices", nullptr);

	bool started{ false };

	if (m_pImpl->pLoop == nullptr)
	{
		gLog.Error("Could not create the device loop.");
	}
	else if (pw_thread_loop_start(m_pImpl->pLoop) < 0)
	{
		gLog.Error("Could not start the device loop.");
	}
	else
	{
		pw_thread_loop_lock(m_pImpl->pLoop);

		m_pImpl->pContext = pw_context_new(pw_thread_loop_get_loop(m_pImpl->pLoop), nullptr, 0);

		if (m_pImpl->pContext == nullptr)
		{
			gLog.Error("Could not create the device context.");
		}
		else
		{
			m_pImpl->pCore = pw_context_connect(m_pImpl->pContext, nullptr, 0);

			if (m_pImpl->pCore == nullptr)
			{
				gLog.Error("Could not connect to PipeWire to list audio devices.");
			}
			else
			{
				pw_core_add_listener(m_pImpl->pCore, &m_pImpl->coreListener, &CoreEvents, m_pImpl);

				m_pImpl->pRegistry = pw_core_get_registry(m_pImpl->pCore, PW_VERSION_REGISTRY, 0);

				if (m_pImpl->pRegistry == nullptr)
				{
					gLog.Error("Could not read the PipeWire registry.");
				}
				else
				{
					pw_registry_add_listener(m_pImpl->pRegistry, &m_pImpl->registryListener,
					                         &RegistryEvents, m_pImpl);
					started = true;
				}
			}
		}

		pw_thread_loop_unlock(m_pImpl->pLoop);
	}

	if (!started)
	{
		Terminate();
	}

	return started;
}

//////////////////////////////////////////////////////////////////////////
void CAudioDevices::Terminate()
{
	if (m_pImpl != nullptr)
	{
		if (m_pImpl->pLoop != nullptr)
		{
			pw_thread_loop_stop(m_pImpl->pLoop);
		}

		if (m_pImpl->pRegistry != nullptr)
		{
			pw_proxy_destroy(reinterpret_cast<pw_proxy*>(m_pImpl->pRegistry));
		}

		if (m_pImpl->pCore != nullptr)
		{
			pw_core_disconnect(m_pImpl->pCore);
		}

		if (m_pImpl->pContext != nullptr)
		{
			pw_context_destroy(m_pImpl->pContext);
		}

		if (m_pImpl->pLoop != nullptr)
		{
			pw_thread_loop_destroy(m_pImpl->pLoop);
		}

		delete m_pImpl;
		m_pImpl = nullptr;
	}

	m_sinks.clear();
	m_sources.clear();
}

//////////////////////////////////////////////////////////////////////////
void CAudioDevices::Refresh()
{
	TGE_PROFILE_SCOPE_N("Devices: refresh");

	m_sinks.clear();
	m_sources.clear();

	if (m_pImpl != nullptr)
	{
		pw_thread_loop_lock(m_pImpl->pLoop);

		m_pImpl->synced = false;
		m_pImpl->pendingSeq = pw_core_sync(m_pImpl->pCore, PW_ID_CORE, 0);

		timespec deadline{};
		pw_thread_loop_get_time(m_pImpl->pLoop, &deadline, SyncTimeoutNs);

		int waited{ 0 };

		while (!m_pImpl->synced && waited == 0)
		{
			waited = pw_thread_loop_timed_wait_full(m_pImpl->pLoop, &deadline);
		}

		for (CDevicesImpl::SNode const& node : m_pImpl->nodes)
		{
			if (node.isSink)
			{
				m_sinks.push_back(node.device);
			}
			else
			{
				m_sources.push_back(node.device);
			}
		}

		pw_thread_loop_unlock(m_pImpl->pLoop);
	}
}

//////////////////////////////////////////////////////////////////////////
std::vector<SAudioDevice> const& CAudioDevices::GetSinks() const
{
	return m_sinks;
}

//////////////////////////////////////////////////////////////////////////
std::vector<SAudioDevice> const& CAudioDevices::GetSources() const
{
	return m_sources;
}
} // namespace Klip::Capture
