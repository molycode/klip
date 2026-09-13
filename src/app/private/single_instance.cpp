#include "single_instance.hpp"

#include "log.hpp"

#include <QtNetwork/QLocalServer>
#include <QtNetwork/QLocalSocket>

#include <unistd.h>

namespace Klip
{
namespace
{
constexpr int ConnectMilliseconds{ 300 };
} // namespace

//////////////////////////////////////////////////////////////////////////
CSingleInstance::CSingleInstance(QObject* pParent)
	: QObject(pParent)
{
}

//////////////////////////////////////////////////////////////////////////
bool CSingleInstance::Claim()
{
	m_name = QStringLiteral("klip-%1").arg(::getuid());

	QLocalSocket socket;
	socket.connectToServer(m_name);

	bool claimed{ false };

	if (socket.waitForConnected(ConnectMilliseconds))
	{
		socket.write("show");
		socket.flush();
		socket.waitForBytesWritten(ConnectMilliseconds);
		socket.disconnectFromServer();
	}
	else
	{
		// Nothing answered, so any socket left behind is from a process that is gone. Removing it is only
		// safe because the connect above failed.
		QLocalServer::removeServer(m_name);

		m_pServer = new QLocalServer(this);

		if (m_pServer->listen(m_name))
		{
			connect(m_pServer, &QLocalServer::newConnection, this, &CSingleInstance::OnConnection);
			claimed = true;
		}
		else
		{
			gLog.Warning("Could not claim the single-instance name: {}",
			             m_pServer->errorString().toStdString());

			// Better two windows than none.
			claimed = true;
		}
	}

	return claimed;
}

//////////////////////////////////////////////////////////////////////////
void CSingleInstance::Terminate()
{
	if (m_pServer != nullptr)
	{
		m_pServer->close();
		m_pServer = nullptr;
	}
}

//////////////////////////////////////////////////////////////////////////
void CSingleInstance::OnConnection()
{
	QLocalSocket* const pSocket{ m_pServer->nextPendingConnection() };

	if (pSocket != nullptr)
	{
		connect(pSocket, &QLocalSocket::disconnected, pSocket, &QLocalSocket::deleteLater);
		Q_EMIT ShowRequested();
	}
}
} // namespace Klip
