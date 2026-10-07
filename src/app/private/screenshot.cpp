#include "screenshot.hpp"

#include "bus/connection.hpp"
#include "bus/portal.hpp"
#include "log.hpp"

#include <QtCore/QEventLoop>
#include <QtCore/QFile>
#include <QtCore/QMetaObject>
#include <QtCore/QRect>
#include <QtCore/QTimer>
#include <QtCore/QUrl>
#include <systemd/sd-bus.h>

namespace Klip
{
namespace
{
constexpr char const* ScreenshotInterface{ "org.freedesktop.portal.Screenshot" };

// The portal puts a permission dialog in front of the first request a user ever makes.
constexpr int AnswerTimeoutMilliseconds{ 30000 };
} // namespace

//////////////////////////////////////////////////////////////////////////
QImage CScreenshot::Take(QRect const& area)
{
	QImage image;
	bool sent{ false };

	m_uri.clear();

	Bus::gConnection.Run([this, &sent](sd_bus* pBus) {
		std::string const token{ Bus::MakeHandleToken() };
		std::string const path{ Bus::GetRequestPath(pBus, token) };

		sd_bus_message_handler_t const onResponse{ [](sd_bus_message* pMessage, void* pScreenshot, sd_bus_error*) {
			static_cast<CScreenshot*>(pScreenshot)->OnResponse(pMessage);

			return 0;
		} };

		sd_bus_error error{ SD_BUS_ERROR_NULL };
		sd_bus_message* pCall{ nullptr };

		// Subscribes before the call goes out; the portal can answer first.
		int result{ sd_bus_match_signal(pBus, &m_pSlot, Bus::PortalService, path.c_str(), Bus::RequestInterface,
		                                "Response", onResponse, this) };

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
		}

		sent = result >= 0;

		sd_bus_message_unref(pCall);
		sd_bus_error_free(&error);
	});

	if (sent)
	{
		QEventLoop loop;
		m_pLoop = &loop;

		QTimer::singleShot(AnswerTimeoutMilliseconds, &loop, &QEventLoop::quit);
		loop.exec();

		m_pLoop = nullptr;
	}

	// No answer can arrive once this returns, so m_uri is this thread's to read from here.
	Bus::gConnection.Run([this](sd_bus*) { m_pSlot = sd_bus_slot_unref(m_pSlot); });

	if (sent && m_uri.empty())
	{
		gLog.Error("The screenshot portal never answered.");
	}
	else if (sent)
	{
		QString const path{ QUrl{ QString::fromStdString(m_uri) }.toLocalFile() };

		if (!image.load(path))
		{
			gLog.Error("Cannot read the screenshot at {}", path.toStdString());
		}

		// The portal writes one file per request and hands it over.
		if (!QFile::remove(path))
		{
			gLog.Warning("Cannot delete the screenshot at {}", path.toStdString());
		}
	}

	if (!image.isNull())
	{
		if (!QRect{ QPoint{ 0, 0 }, image.size() }.contains(area))
		{
			gLog.Warning("The screenshot is {}x{} and does not hold the {}x{} at {},{} it was asked for.",
			             image.width(), image.height(), area.width(), area.height(), area.x(), area.y());
		}

		// One image carries every output, laid out the way the desktop is.
		image = image.copy(area);
	}

	return image;
}

//////////////////////////////////////////////////////////////////////////
void CScreenshot::OnResponse(sd_bus_message* pMessage)
{
	uint32_t response{ 0 };

	m_pSlot = sd_bus_slot_unref(m_pSlot);

	if (sd_bus_message_read(pMessage, "u", &response) >= 0 && response == Bus::ResponseSuccess)
	{
		Bus::ReadDict(pMessage, [this](std::string_view key, sd_bus_message* pEntry) {
			return key == "uri" && Bus::ReadString(pEntry, m_uri);
		});
	}
	else
	{
		gLog.Error("Screenshot returned {}.", response);
	}

	// The loop is the UI thread's, and the wake carries nothing.
	QMetaObject::invokeMethod(this, [this]() {
		if (m_pLoop != nullptr)
		{
			m_pLoop->quit();
		}
	}, Qt::QueuedConnection);
}
} // namespace Klip
