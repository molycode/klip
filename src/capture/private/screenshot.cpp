#include "capture/screenshot.hpp"

#include "bus/connection.hpp"
#include "bus/file_uri.hpp"
#include "bus/portal.hpp"
#include "log.hpp"

#include <systemd/sd-bus.h>

#include <filesystem>
#include <string_view>
#include <system_error>
#include <utility>

namespace Klip::Capture
{
namespace
{
constexpr char const* ScreenshotInterface{ "org.freedesktop.portal.Screenshot" };

//////////////////////////////////////////////////////////////////////////
int ForwardResponse(sd_bus_message* pMessage, void* pScreenshot, sd_bus_error*)
{
	static_cast<CScreenshot*>(pScreenshot)->OnResponse(pMessage);

	return 0;
}

//////////////////////////////////////////////////////////////////////////
void Delete(std::string const& path)
{
	std::error_code error{};

	std::filesystem::remove(path, error);

	if (error.value() != 0)
	{
		gLog.Warning("Cannot delete the screenshot at {}: {}", path, error.message());
	}
}
} // namespace

//////////////////////////////////////////////////////////////////////////
bool CScreenshot::Request(WakeCallback wake)
{
	bool sent{ false };

	m_wake = std::move(wake);
	m_path.clear();
	m_isAnswered.store(false, std::memory_order_relaxed);

	Bus::gConnection.Run([this, &sent](sd_bus* pBus) {
		std::string const token{ Bus::MakeHandleToken() };
		std::string const path{ Bus::GetRequestPath(pBus, token) };

		sd_bus_error    error{ SD_BUS_ERROR_NULL };
		sd_bus_message* pCall{ nullptr };

		// Subscribes before the call goes out; the portal can answer first.
		int result{ sd_bus_match_signal(pBus, &m_pSlot, Bus::PortalService, path.c_str(), Bus::RequestInterface,
		                                "Response", ForwardResponse, this) };

		if (result >= 0)
		{
			result = sd_bus_message_new_method_call(pBus, &pCall, Bus::PortalService, Bus::PortalPath,
			                                        ScreenshotInterface, "Screenshot");
		}

		if (result >= 0)
		{
			result = sd_bus_message_append(pCall, "s", "");
		}

		if (result >= 0)
		{
			result = Bus::AppendOptions(pCall, { { "handle_token", token }, { "interactive", false } });
		}

		if (result >= 0)
		{
			result = sd_bus_call(pBus, pCall, 0, &error, nullptr);
		}

		if (result < 0)
		{
			gLog.Error("Screenshot failed: {}", Bus::Describe(error, result));
			m_pSlot = sd_bus_slot_unref(m_pSlot);
		}

		sent = result >= 0;

		sd_bus_message_unref(pCall);
		sd_bus_error_free(&error);
	});

	return sent;
}

//////////////////////////////////////////////////////////////////////////
// With the slot gone on the bus thread, no answer and no wake can follow.
void CScreenshot::Cancel()
{
	Bus::gConnection.Run([this](sd_bus*) { m_pSlot = sd_bus_slot_unref(m_pSlot); });

	std::optional<std::string> const answer{ TakeAnswer() };

	if (answer.has_value() && !answer->empty())
	{
		Delete(*answer);
	}
}

//////////////////////////////////////////////////////////////////////////
// The file is the caller's from here, to delete once read.
std::optional<std::string> CScreenshot::TakeAnswer()
{
	std::optional<std::string> answer{};

	if (m_isAnswered.exchange(false, std::memory_order_acquire))
	{
		answer = std::move(m_path);
		m_path.clear();
	}

	return answer;
}

//////////////////////////////////////////////////////////////////////////
// On Bus::gConnection's thread; the wake asks the owner's to take the answer.
void CScreenshot::OnResponse(sd_bus_message* pMessage)
{
	uint32_t    response{ 0 };
	std::string uri{};

	m_pSlot = sd_bus_slot_unref(m_pSlot);

	if (sd_bus_message_read(pMessage, "u", &response) >= 0 && response == Bus::ResponseSuccess)
	{
		Bus::ReadDict(pMessage, [&uri](std::string_view key, sd_bus_message* pEntry) {
			return key == "uri" && Bus::ReadString(pEntry, uri);
		});

		m_path = Bus::ToLocalPath(uri);

		if (m_path.empty())
		{
			gLog.Error("The screenshot portal answered with '{}', which is not a local file", uri);
		}
	}
	else
	{
		gLog.Error("Screenshot returned {}.", response);
	}

	m_isAnswered.store(true, std::memory_order_release);
	m_wake();
}
} // namespace Klip::Capture
