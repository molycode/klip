#include "tray_icon.hpp"

#include "log.hpp"
#include "tray_adaptor.hpp"
#include "tray_menu.hpp"

#include <QtCore/QtEndian>
#include <QtDBus/QDBusConnection>
#include <QtDBus/QDBusInterface>
#include <QtDBus/QDBusMessage>
#include <QtGui/QImage>
#include <QtGui/QPainter>
#include <tge/profiling/profiling.hpp>

#include <unistd.h>

namespace Klip
{
namespace
{
constexpr int IconSize{ 64 };
constexpr qreal FrameStroke{ 5.0 };
constexpr qreal FrameRadius{ 7.0 };
constexpr qreal DotRadius{ 8.0 };

constexpr char const* WatcherService{ "org.kde.StatusNotifierWatcher" };
constexpr char const* WatcherPath{ "/StatusNotifierWatcher" };

STrayImage MakeImage(bool recording)
{
	QColor const frameColour{ 190, 190, 190 };
	QColor const dotColour{ recording ? QColor{ 225, 60, 60 } : frameColour };

	QImage image{ IconSize, IconSize, QImage::Format_ARGB32 };
	image.fill(Qt::transparent);

	QPainter painter{ &image };
	painter.setRenderHint(QPainter::Antialiasing);

	QRectF const frame{ QPointF{ 7.0, 13.0 }, QSizeF{ 50.0, 38.0 } };
	painter.setPen(QPen{ frameColour, FrameStroke, Qt::SolidLine, Qt::FlatCap, Qt::RoundJoin });
	painter.setBrush(Qt::NoBrush);
	painter.drawRoundedRect(frame.adjusted(FrameStroke / 2.0, FrameStroke / 2.0,
	                                       -FrameStroke / 2.0, -FrameStroke / 2.0),
	                        FrameRadius, FrameRadius);

	QPointF const centre{ frame.center() };

	if (recording)
	{
		painter.setPen(Qt::NoPen);
		painter.setBrush(dotColour);
		painter.drawEllipse(centre, DotRadius, DotRadius);
	}
	else
	{
		painter.setPen(QPen{ dotColour, 4.0 });
		painter.setBrush(Qt::NoBrush);
		painter.drawEllipse(centre, DotRadius - 2.0, DotRadius - 2.0);
	}

	painter.end();

	STrayImage result;
	result.width = image.width();
	result.height = image.height();
	result.pixels.resize(static_cast<qsizetype>(image.sizeInBytes()));

	// The specification asks for network byte order; QImage holds ARGB32 in the host's.
	uint32_t const* pSource{ reinterpret_cast<uint32_t const*>(image.constBits()) };
	uint32_t* pTarget{ reinterpret_cast<uint32_t*>(result.pixels.data()) };

	for (int index{ 0 }; index < image.width() * image.height(); ++index)
	{
		pTarget[index] = qToBigEndian(pSource[index]);
	}

	return result;
}
} // namespace

//////////////////////////////////////////////////////////////////////////
CTrayIcon::CTrayIcon(QObject* pParent)
	: QObject(pParent)
{
}

//////////////////////////////////////////////////////////////////////////
bool CTrayIcon::Initialize()
{
	TGE_PROFILE_SCOPE_N("Startup: tray");

	RegisterTrayTypes();
	RegisterMenuTypes();

	m_pMenuObject = new QObject(this);
	m_pAdaptor = new CTrayAdaptor(this);
	m_pMenu = new CTrayMenu(this, m_pMenuObject);

	m_serviceName = QStringLiteral("org.kde.StatusNotifierItem-%1-1").arg(::getpid());

	QDBusConnection bus{ QDBusConnection::sessionBus() };

	if (!bus.registerService(m_serviceName))
	{
		gLog.Warning("Could not take the tray service name.");
	}
	else if (!bus.registerObject(QStringLiteral("/StatusNotifierItem"), this,
	                             QDBusConnection::ExportAdaptors))
	{
		gLog.Warning("Could not publish the tray item.");
	}
	else if (!bus.registerObject(QStringLiteral("/MenuBar"), m_pMenuObject,
	                             QDBusConnection::ExportAdaptors))
	{
		gLog.Warning("Could not publish the tray menu.");
	}
	else
	{
		QDBusInterface watcher{ QLatin1String(WatcherService), QLatin1String(WatcherPath),
			                    QLatin1String(WatcherService), bus };

		if (!watcher.isValid())
		{
			gLog.Warning("No system tray: Klip will stay on screen while recording, and so appear in it.");
		}
		else
		{
			QDBusMessage const reply{ watcher.call(QStringLiteral("RegisterStatusNotifierItem"),
			                                       m_serviceName) };

			if (reply.type() == QDBusMessage::ErrorMessage)
			{
				gLog.Warning("The tray refused Klip's item: {}", reply.errorMessage().toStdString());
			}
			else
			{
				m_registered = true;
			}
		}
	}

	return m_registered;
}

//////////////////////////////////////////////////////////////////////////
void CTrayIcon::Terminate()
{
	if (!m_serviceName.isEmpty())
	{
		QDBusConnection bus{ QDBusConnection::sessionBus() };
		bus.unregisterObject(QStringLiteral("/StatusNotifierItem"));
		bus.unregisterObject(QStringLiteral("/MenuBar"));
		bus.unregisterService(m_serviceName);
		m_serviceName.clear();
	}

	m_registered = false;
}

//////////////////////////////////////////////////////////////////////////
TrayImageList CTrayIcon::IconImages() const
{
	TrayImageList images;
	images.append(MakeImage(m_recording));

	return images;
}

//////////////////////////////////////////////////////////////////////////
QString CTrayIcon::TitleText() const
{
	return m_recording ? QStringLiteral("Klip — recording") : QStringLiteral("Klip");
}

//////////////////////////////////////////////////////////////////////////
void CTrayIcon::SetRecording(bool recording)
{
	if (m_recording != recording)
	{
		m_recording = recording;

		if (m_registered)
		{
			Q_EMIT m_pAdaptor->NewIcon();
			Q_EMIT m_pAdaptor->NewTitle();
			Q_EMIT m_pAdaptor->NewToolTip();
			m_pMenu->AnnounceChange();
		}

		if (!recording)
		{
			SetLabel(QStringLiteral("Klip"));
			SetDetail(QString{});
		}
	}
}

//////////////////////////////////////////////////////////////////////////
void CTrayIcon::SetLabel(QString const& label)
{
	if (m_label != label)
	{
		m_label = label;

		if (m_registered)
		{
			Q_EMIT m_pAdaptor->XAyatanaNewLabel(m_label, m_pAdaptor->XAyatanaLabelGuide());
		}
	}
}

//////////////////////////////////////////////////////////////////////////
void CTrayIcon::SetDetail(QString const& detail)
{
	if (m_detail != detail)
	{
		m_detail = detail;

		if (m_registered)
		{
			Q_EMIT m_pAdaptor->NewToolTip();
		}
	}
}
} // namespace Klip
