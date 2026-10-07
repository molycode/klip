#pragma once

#include <tge/non_copyable.hpp>

#include <atomic>
#include <functional>
#include <optional>
#include <string>

struct sd_bus_message;
struct sd_bus_slot;

namespace Klip::Capture
{
class CScreenshot final : private Tge::SNoCopyNoMove
{
public:

	using WakeCallback = std::function<void()>;

	CScreenshot() = default;
	~CScreenshot() = default;

	bool                       Request(WakeCallback wake);
	void                       Cancel();
	std::optional<std::string> TakeAnswer();

	void OnResponse(sd_bus_message* pMessage);

private:

	WakeCallback m_wake;

	std::string       m_path;
	std::atomic<bool> m_isAnswered{ false };

	sd_bus_slot* m_pSlot{ nullptr };
};
} // namespace Klip::Capture
