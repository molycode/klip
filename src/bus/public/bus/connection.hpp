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
class CConnection final : private Tge::SNoCopyNoMove
{
public:

	using Task = std::function<void(sd_bus*)>;

	CConnection() = default;
	~CConnection() = default;

	bool Initialize(std::string_view threadName);

	void Terminate();

	void Post(Task task);

	void Run(Task task);

private:

	void Pump();

	Tge::Threading::CEventLoop              m_loop;
	std::optional<Tge::Threading::SWatchId> m_watch;
	sd_bus*                                 m_pBus{ nullptr };
};

extern CConnection gConnection;
} // namespace Klip::Bus
