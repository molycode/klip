#include "capture/portal_session.hpp"

#include "log.hpp"

#include <QtCore/QRandomGenerator>
#include <QtCore/QSettings>
#include <QtDBus/QDBusConnection>
#include <QtDBus/QDBusInterface>
#include <QtDBus/QDBusMetaType>
#include <QtDBus/QDBusReply>
#include <QtDBus/QDBusUnixFileDescriptor>
#include <tge/profiling/profiling.hpp>

#include <unistd.h>

namespace Klip::Capture
{
// a(ua{sv})
struct SPortalStream final
{
	uint32_t    nodeId{ 0 };
	QVariantMap properties;
};
} // namespace Klip::Capture

Q_DECLARE_METATYPE(Klip::Capture::SPortalStream)
Q_DECLARE_METATYPE(QList<Klip::Capture::SPortalStream>)

namespace Klip::Capture
{
namespace
{
constexpr char const* PortalService{ "org.freedesktop.portal.Desktop" };
constexpr char const* PortalPath{ "/org/freedesktop/portal/desktop" };
constexpr char const* ScreenCastInterface{ "org.freedesktop.portal.ScreenCast" };
constexpr char const* RequestInterface{ "org.freedesktop.portal.Request" };
constexpr char const* SessionInterface{ "org.freedesktop.portal.Session" };
constexpr char const* ScreenTokenKey{ "portal/restoreToken/screen" };
constexpr char const* WindowTokenKey{ "portal/restoreToken/window" };

// Fixed by the ScreenCast portal specification.
constexpr uint32_t SourceTypeMonitor{ 1 };
constexpr uint32_t SourceTypeWindow{ 2 };
constexpr uint32_t CursorModeEmbedded{ 2 };
constexpr uint32_t DoNotPersist{ 0 };
constexpr uint32_t PersistUntilRevoked{ 2 };

constexpr uint32_t ResponseSuccess{ 0 };
constexpr uint32_t ResponseCancelled{ 1 };

// persist_mode and restore_token arrived in 4.
constexpr uint32_t MinimumPortalVersion{ 4 };

uint32_t ReadPortalProperty(char const* pName)
{
	QDBusInterface properties{ PortalService, PortalPath, "org.freedesktop.DBus.Properties",
	                           QDBusConnection::sessionBus() };

	QDBusReply<QVariant> const reply{ properties.call("Get", ScreenCastInterface, pName) };

	return reply.isValid() ? reply.value().toUInt() : 0;
}

bool ReadSize(QVariantMap const& properties, uint32_t& width, uint32_t& height)
{
	bool read{ false };

	if (properties.contains("size"))
	{
		QDBusArgument const size{ properties.value("size").value<QDBusArgument>() };

		int32_t rawWidth{ 0 };
		int32_t rawHeight{ 0 };

		size.beginStructure();
		size >> rawWidth >> rawHeight;
		size.endStructure();

		if (rawWidth > 0 && rawHeight > 0)
		{
			width = static_cast<uint32_t>(rawWidth);
			height = static_cast<uint32_t>(rawHeight);
			read = true;
		}
	}

	return read;
}
} // namespace

QDBusArgument& operator<<(QDBusArgument& argument, SPortalStream const& stream)
{
	argument.beginStructure();
	argument << stream.nodeId << stream.properties;
	argument.endStructure();

	return argument;
}

//////////////////////////////////////////////////////////////////////////
QDBusArgument const& operator>>(QDBusArgument const& argument, SPortalStream& stream)
{
	argument.beginStructure();
	argument >> stream.nodeId >> stream.properties;
	argument.endStructure();

	return argument;
}

//////////////////////////////////////////////////////////////////////////
CPortalSession::CPortalSession(QObject* pParent)
	: QObject(pParent)
{
}

//////////////////////////////////////////////////////////////////////////
bool CPortalSession::Initialize()
{
	TGE_PROFILE_SCOPE_N("Portal: initialize");

	qDBusRegisterMetaType<SPortalStream>();
	qDBusRegisterMetaType<QList<SPortalStream>>();

	uint32_t const version{ ReadPortalProperty("version") };

	if (version == 0)
	{
		gLog.Error("No ScreenCast portal on the session bus. Install xdg-desktop-portal and a backend for "
		           "your desktop (xdg-desktop-portal-gnome, -kde or -wlr).");
	}
	else if (version < MinimumPortalVersion)
	{
		gLog.Error("ScreenCast portal is version {}; Klip needs {} for persist_mode.", version,
		           MinimumPortalVersion);
	}
	else
	{
		m_availableSourceTypes = ReadPortalProperty("AvailableSourceTypes");
		m_availableCursorModes = ReadPortalProperty("AvailableCursorModes");

		if ((m_availableSourceTypes & SourceTypeMonitor) == 0)
		{
			gLog.Error("The ScreenCast portal offers no monitor source.");
		}
		else
		{
			gLog.Info("ScreenCast portal v{}, sources 0x{:x}, cursor modes 0x{:x}", version,
			          m_availableSourceTypes, m_availableCursorModes);

			m_initialized = true;
		}
	}

	return m_initialized;
}

//////////////////////////////////////////////////////////////////////////
void CPortalSession::Close()
{
	TGE_PROFILE_SCOPE_N("Portal: close");

	if (!m_sessionHandle.isEmpty())
	{
		// Cleared before the call, and left cleared: the Closed that follows is ours, and it arrives
		// whenever D-Bus gets round to it rather than inside this function.
		m_sessionLive = false;

		QDBusConnection::sessionBus().disconnect(PortalService, m_sessionHandle, SessionInterface,
		                                         QStringLiteral("Closed"), this, SLOT(OnSessionClosed()));

		QDBusInterface session{ PortalService, m_sessionHandle, SessionInterface,
			                    QDBusConnection::sessionBus() };
		QDBusMessage const reply{ session.call("Close") };

		if (reply.type() == QDBusMessage::ErrorMessage)
		{
			gLog.Warning("Closing the portal session failed: {}", reply.errorMessage().toStdString());
		}

		m_sessionHandle.clear();
	}
}

//////////////////////////////////////////////////////////////////////////
void CPortalSession::OnSessionClosed()
{
	if (m_sessionLive)
	{
		m_sessionLive = false;

		gLog.Warning("The compositor ended the screen cast.");

		m_sessionHandle.clear();

		if (m_onClosed)
		{
			m_onClosed();
		}
	}
}

//////////////////////////////////////////////////////////////////////////
void CPortalSession::Terminate()
{
	Close();

	m_callback = nullptr;
	m_onClosed = nullptr;
	m_requestPath.clear();
	m_initialized = false;
	m_busy = false;
}

//////////////////////////////////////////////////////////////////////////
QString CPortalSession::MakeToken()
{
	++m_tokenCounter;

	return QStringLiteral("klip_%1_%2").arg(QRandomGenerator::global()->generate()).arg(m_tokenCounter);
}

//////////////////////////////////////////////////////////////////////////
QString CPortalSession::PrepareRequest(QString const& token, char const* pSlot)
{
	QString sender{ QDBusConnection::sessionBus().baseService().mid(1) };
	sender.replace('.', '_');

	m_requestPath = QStringLiteral("/org/freedesktop/portal/desktop/request/%1/%2").arg(sender, token);

	QDBusConnection::sessionBus().connect(PortalService, m_requestPath, RequestInterface,
	                                      QStringLiteral("Response"), this, pSlot);

	return m_requestPath;
}

//////////////////////////////////////////////////////////////////////////
void CPortalSession::Start(ESourceType source, bool rememberWindow, ResultCallback callback)
{
	TGE_PROFILE_SCOPE_N("Portal: create session");

	m_source = source;

	m_rememberWindow = rememberWindow;

	// A screen is the same screen next time, so its grant is always worth keeping. Which window someone
	// wants is a fresh question unless they say otherwise, so that grant is dropped and the picker returns.
	QSettings settings;

	if (source != ESourceType::Screen && !m_rememberWindow)
	{
		settings.remove(WindowTokenKey);
	}

	m_restoreToken =
		settings.value(source == ESourceType::Screen ? ScreenTokenKey : WindowTokenKey).toString();

	if (!m_initialized || m_busy)
	{
		gLog.Error("Start called on a portal session that is {}.", m_busy ? "already running" : "not ready");
		callback(EPortalResult::Failed, SStreamInfo{}, -1);
	}
	else
	{
		m_callback = std::move(callback);
		m_busy = true;

		QString const token{ MakeToken() };
		PrepareRequest(token, SLOT(OnCreateSessionResponse(uint, QVariantMap)));

		QVariantMap options;
		options["handle_token"] = token;
		options["session_handle_token"] = MakeToken();

		QDBusInterface screenCast{ PortalService, PortalPath, ScreenCastInterface,
		                           QDBusConnection::sessionBus() };

		QDBusReply<QDBusObjectPath> const reply{ screenCast.call("CreateSession", options) };

		if (!reply.isValid())
		{
			gLog.Error("CreateSession failed: {}", reply.error().message().toStdString());
			Finish(EPortalResult::Failed, SStreamInfo{}, -1);
		}
	}
}

//////////////////////////////////////////////////////////////////////////
void CPortalSession::OnCreateSessionResponse(uint response, QVariantMap const& results)
{
	TGE_PROFILE_SCOPE_N("Portal: select sources");

	QDBusConnection::sessionBus().disconnect(PortalService, m_requestPath, RequestInterface,
	                                         QStringLiteral("Response"), this,
	                                         SLOT(OnCreateSessionResponse(uint, QVariantMap)));

	if (response != ResponseSuccess)
	{
		gLog.Error("CreateSession returned {}.", response);
		Finish(EPortalResult::Failed, SStreamInfo{}, -1);
	}
	else
	{
		m_sessionHandle = results.value("session_handle").toString();

		// The spec's own notice that a cast has ended. The PipeWire stream does not reliably say so.
		QDBusConnection::sessionBus().connect(PortalService, m_sessionHandle, SessionInterface,
		                                      QStringLiteral("Closed"), this, SLOT(OnSessionClosed()));

		m_sessionLive = true;

		QString const token{ MakeToken() };
		PrepareRequest(token, SLOT(OnSelectSourcesResponse(uint, QVariantMap)));

		QVariantMap options;
		options["handle_token"] = token;
		options["types"] = m_source == ESourceType::Window ? SourceTypeWindow : SourceTypeMonitor;
		options["multiple"] = false;
		options["cursor_mode"] = CursorModeEmbedded;
		options["persist_mode"] = m_source == ESourceType::Window && !m_rememberWindow
		                              ? DoNotPersist
		                              : PersistUntilRevoked;

		if (!m_restoreToken.isEmpty())
		{
			options["restore_token"] = m_restoreToken;
		}

		QDBusInterface screenCast{ PortalService, PortalPath, ScreenCastInterface,
		                           QDBusConnection::sessionBus() };

		QDBusReply<QDBusObjectPath> const reply{
			screenCast.call("SelectSources", QVariant::fromValue(QDBusObjectPath(m_sessionHandle)), options)
		};

		if (!reply.isValid())
		{
			gLog.Error("SelectSources failed: {}", reply.error().message().toStdString());
			Finish(EPortalResult::Failed, SStreamInfo{}, -1);
		}
	}
}

//////////////////////////////////////////////////////////////////////////
void CPortalSession::OnSelectSourcesResponse(uint response, QVariantMap const& results)
{
	TGE_PROFILE_SCOPE_N("Portal: start");

	QDBusConnection::sessionBus().disconnect(PortalService, m_requestPath, RequestInterface,
	                                         QStringLiteral("Response"), this,
	                                         SLOT(OnSelectSourcesResponse(uint, QVariantMap)));

	if (response != ResponseSuccess)
	{
		gLog.Error("SelectSources returned {}.", response);
		Finish(EPortalResult::Failed, SStreamInfo{}, -1);
	}
	else
	{
		QString const token{ MakeToken() };
		PrepareRequest(token, SLOT(OnStartResponse(uint, QVariantMap)));

		QVariantMap options;
		options["handle_token"] = token;

		QDBusInterface screenCast{ PortalService, PortalPath, ScreenCastInterface,
		                           QDBusConnection::sessionBus() };

		QDBusReply<QDBusObjectPath> const reply{
			screenCast.call("Start", QVariant::fromValue(QDBusObjectPath(m_sessionHandle)), QString{}, options)
		};

		if (!reply.isValid())
		{
			gLog.Error("Start failed: {}", reply.error().message().toStdString());
			Finish(EPortalResult::Failed, SStreamInfo{}, -1);
		}
	}
}

//////////////////////////////////////////////////////////////////////////
void CPortalSession::OnStartResponse(uint response, QVariantMap const& results)
{
	TGE_PROFILE_SCOPE_N("Portal: open remote");

	QDBusConnection::sessionBus().disconnect(PortalService, m_requestPath, RequestInterface,
	                                         QStringLiteral("Response"), this,
	                                         SLOT(OnStartResponse(uint, QVariantMap)));

	if (response == ResponseCancelled)
	{
		gLog.Warning("The screen-cast request was dismissed.");
		Finish(EPortalResult::Cancelled, SStreamInfo{}, -1);
	}
	else if (response != ResponseSuccess)
	{
		gLog.Error("Start returned {}.", response);
		Finish(EPortalResult::Failed, SStreamInfo{}, -1);
	}
	else
	{
		QString const restoreToken{ results.value("restore_token").toString() };

		bool const keeps{ m_source == ESourceType::Screen || m_rememberWindow };

		if (keeps && !restoreToken.isEmpty() && restoreToken != m_restoreToken)
		{
			m_restoreToken = restoreToken;

			QSettings settings;
			settings.setValue(m_source == ESourceType::Window ? WindowTokenKey : ScreenTokenKey,
			                  m_restoreToken);
		}

		QList<SPortalStream> const streams{
			qdbus_cast<QList<SPortalStream>>(results.value("streams"))
		};

		SStreamInfo info;

		if (streams.size() > 1)
		{
			// multiple is already false in SelectSources; some pickers offer a choice of several anyway.
			gLog.Warning("{} sources were shared; Klip records the first.", streams.size());
		}

		if (streams.isEmpty())
		{
			gLog.Error("The portal granted the request but returned no stream.");
			Finish(EPortalResult::Failed, info, -1);
		}
		else if (!ReadSize(streams.first().properties, info.width, info.height))
		{
			gLog.Error("The granted stream carries no usable size.");
			Finish(EPortalResult::Failed, info, -1);
		}
		else
		{
			info.nodeId = streams.first().nodeId;


			QDBusInterface screenCast{ PortalService, PortalPath, ScreenCastInterface,
			                           QDBusConnection::sessionBus() };

			QDBusReply<QDBusUnixFileDescriptor> const reply{
				screenCast.call("OpenPipeWireRemote", QVariant::fromValue(QDBusObjectPath(m_sessionHandle)),
				                QVariantMap{})
			};

			if (!reply.isValid())
			{
				gLog.Error("OpenPipeWireRemote failed: {}", reply.error().message().toStdString());
				Finish(EPortalResult::Failed, info, -1);
			}
			else
			{
				// QDBusUnixFileDescriptor closes its copy when the reply dies.
				int const pipeWireFd{ ::dup(reply.value().fileDescriptor()) };

				if (pipeWireFd < 0)
				{
					gLog.Error("Could not duplicate the PipeWire descriptor.");
					Finish(EPortalResult::Failed, info, -1);
				}
				else
				{
					gLog.Info("Capturing node {} at {}x{}", info.nodeId, info.width, info.height);
					Finish(EPortalResult::Success, info, pipeWireFd);
				}
			}
		}
	}
}

//////////////////////////////////////////////////////////////////////////
void CPortalSession::Finish(EPortalResult result, SStreamInfo const& info, int pipeWireFd)
{
	// A refused or abandoned request leaves a session the portal will close by itself, which would then
	// read as the compositor withdrawing a cast that never started.
	if (result != EPortalResult::Success)
	{
		Close();
	}

	m_busy = false;

	ResultCallback callback{ std::move(m_callback) };
	m_callback = nullptr;

	if (callback)
	{
		callback(result, info, pipeWireFd);
	}
}
} // namespace Klip::Capture
