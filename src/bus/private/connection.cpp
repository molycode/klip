#include "bus/connection.hpp"

#include "log.hpp"

#include <systemd/sd-bus.h>
#include <tge/assert.hpp>

#include <cstdint>
#include <poll.h>
#include <semaphore>
#include <system_error>

namespace Klip::Bus
{
CConnection gConnection;

//////////////////////////////////////////////////////////////////////////
bool CConnection::Initialize(std::string_view threadName)
{
	bool initialized{ false };
	int const opened{ sd_bus_open_user(&m_pBus) };

	if (opened < 0)
	{
		gLog.Error("Cannot connect to the session bus: {}", std::generic_category().message(-opened));
	}
	else
	{
		// Here, before the loop owns the bus: it blocks until the bus has answered Hello.
		char const* pUniqueName{ nullptr };
		int const named{ sd_bus_get_unique_name(m_pBus, &pUniqueName) };

		if (named < 0)
		{
			gLog.Error("The session bus did not answer Hello: {}", std::generic_category().message(-named));
		}
		else if (!m_loop.Initialize(threadName))
		{
			gLog.Error("Cannot start the {} thread.", threadName);
		}
		else
		{
			m_loop.Post([this]() {
				m_watch = m_loop.Watch(sd_bus_get_fd(m_pBus), [this]() { Pump(); });

				if (!m_watch.has_value())
				{
					gLog.Error("Cannot watch the session bus.");
				}

				Pump();
			});

			initialized = true;
		}
	}

	if (!initialized)
	{
		m_pBus = sd_bus_flush_close_unref(m_pBus);
	}

	return initialized;
}

//////////////////////////////////////////////////////////////////////////
void CConnection::Terminate()
{
	if (m_pBus != nullptr)
	{
		Run([this](sd_bus*) {
			if (m_watch.has_value())
			{
				m_loop.Unwatch(*m_watch);
				m_watch.reset();
			}
		});

		m_loop.Terminate();
		m_pBus = sd_bus_flush_close_unref(m_pBus);
	}
}

//////////////////////////////////////////////////////////////////////////
void CConnection::Post(Task task)
{
	m_loop.Post([this, task = std::move(task)]() {
		task(m_pBus);
		Pump();
	});
}

//////////////////////////////////////////////////////////////////////////
void CConnection::Run(Task task)
{
	TGE_ASSERT(!m_loop.IsLoopThread(), "Run on the bus thread waits for itself");

	std::binary_semaphore done{ 0 };

	m_loop.Post([this, &task, &done]() {
		task(m_pBus);
		Pump();
		done.release();
	});

	done.acquire();
}

//////////////////////////////////////////////////////////////////////////
void CConnection::Pump()
{
	int processed{ 1 };

	while (processed > 0)
	{
		processed = sd_bus_process(m_pBus, nullptr);

		// Unwatched while the descriptor is still open: sd-bus closes it itself a step later, and the number
		// is then free to be reused.
		if (m_watch.has_value() && sd_bus_is_open(m_pBus) <= 0)
		{
			m_loop.Unwatch(*m_watch);
			m_watch.reset();

			gLog.Error("The session bus connection was closed.");
		}
	}

	if (m_watch.has_value())
	{
		if (processed < 0)
		{
			gLog.Error("Processing the session bus failed: {}", std::generic_category().message(-processed));
		}

		// The loop watches for readable only; a send the socket would not take whole waits here instead.
		int const events{ sd_bus_get_events(m_pBus) };

		if (events > 0 && (static_cast<uint32_t>(events) & static_cast<uint32_t>(POLLOUT)) != 0)
		{
			sd_bus_flush(m_pBus);
		}
	}
}
} // namespace Klip::Bus
