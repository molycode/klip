#pragma once

#include <tge/non_copyable.hpp>
#include <tge/threading/event_loop.hpp>
#include <tge/threading/watch_id.hpp>

#include <functional>
#include <optional>
#include <string_view>

struct sd_bus;

namespace Klip::Bus
{
// The session bus on a thread of its own. sd-bus is not thread-safe, so the bus is touched only there, by
// the tasks posted to it; every call those tasks make is synchronous.
class CConnection final : private Tge::SNoCopyNoMove
{
public:

	using Task = std::function<void(sd_bus*)>;

	CConnection() = default;
	~CConnection() = default;

	bool Initialize(std::string_view threadName);

	// Runs everything posted before it first.
	void Terminate();

	void Post(Task task);

	// Returns once the task, and whatever it pulled off the bus, has been dispatched. Never from the bus thread.
	void Run(Task task);

private:

	void Pump();

	Tge::Threading::CEventLoop              m_loop;
	std::optional<Tge::Threading::SWatchId> m_watch;
	sd_bus*                                 m_pBus{ nullptr };
};

extern CConnection gConnection;
} // namespace Klip::Bus
