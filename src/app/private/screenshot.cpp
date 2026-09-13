#include "screenshot.hpp"

#include "log.hpp"

#include <QtCore/QEventLoop>
#include <QtCore/QFile>
#include <QtCore/QRandomGenerator>
#include <QtCore/QRect>
#include <QtCore/QTimer>
#include <QtCore/QUrl>
#include <QtDBus/QDBusConnection>
#include <QtDBus/QDBusInterface>
#include <QtDBus/QDBusObjectPath>
#include <QtDBus/QDBusReply>

namespace Klip
{
namespace
{
constexpr char const* PortalService{ "org.freedesktop.portal.Desktop" };
constexpr char const* PortalPath{ "/org/freedesktop/portal/desktop" };
constexpr char const* ScreenshotInterface{ "org.freedesktop.portal.Screenshot" };
constexpr char const* RequestInterface{ "org.freedesktop.portal.Request" };

constexpr uint32_t ResponseSuccess{ 0 };

// The portal puts a permission dialog in front of the first request a user ever makes.
constexpr int AnswerTimeoutMilliseconds{ 30000 };
} // namespace

//////////////////////////////////////////////////////////////////////////
CScreenshot::CScreenshot(QObject* pParent)
	: QObject(pParent)
{
}

//////////////////////////////////////////////////////////////////////////
QImage CScreenshot::Take(QRect const& area)
{
	QImage image;

	QString sender{ QDBusConnection::sessionBus().baseService().mid(1) };
	sender.replace('.', '_');

	QString const token{ QStringLiteral("klip_%1").arg(QRandomGenerator::global()->generate()) };

	m_requestPath = QStringLiteral("/org/freedesktop/portal/desktop/request/%1/%2").arg(sender, token);
	m_uri.clear();

	// Subscribes before the call goes out; the portal can answer first.
	QDBusConnection::sessionBus().connect(PortalService, m_requestPath, RequestInterface,
	                                      QStringLiteral("Response"), this,
	                                      SLOT(OnResponse(uint, QVariantMap)));

	QVariantMap options;
	options["handle_token"] = token;
	options["interactive"] = false;

	QDBusInterface screenshot{ PortalService, PortalPath, ScreenshotInterface,
	                           QDBusConnection::sessionBus() };

	QDBusReply<QDBusObjectPath> const reply{ screenshot.call("Screenshot", QString{}, options) };

	if (!reply.isValid())
	{
		gLog.Error("Screenshot failed: {}", reply.error().message().toStdString());
	}
	else
	{
		QEventLoop loop;
		m_pLoop = &loop;

		QTimer::singleShot(AnswerTimeoutMilliseconds, &loop, &QEventLoop::quit);
		loop.exec();

		m_pLoop = nullptr;

		if (m_uri.isEmpty())
		{
			gLog.Error("The screenshot portal never answered.");
		}
		else
		{
			QString const path{ QUrl{ m_uri }.toLocalFile() };

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
	}

	QDBusConnection::sessionBus().disconnect(PortalService, m_requestPath, RequestInterface,
	                                         QStringLiteral("Response"), this,
	                                         SLOT(OnResponse(uint, QVariantMap)));

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
void CScreenshot::OnResponse(uint response, QVariantMap const& results)
{
	if (response == ResponseSuccess)
	{
		m_uri = results.value("uri").toString();
	}
	else
	{
		gLog.Error("Screenshot returned {}.", response);
	}

	if (m_pLoop != nullptr)
	{
		m_pLoop->quit();
	}
}
} // namespace Klip
