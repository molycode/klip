#pragma once

#include <tge/non_copyable.hpp>

#include <QtCore/QObject>
#include <QtCore/QString>

class QLocalServer;

namespace Klip
{
class CSingleInstance final : public QObject, private Tge::SNoCopyNoMove
{
	Q_OBJECT

public:

	explicit CSingleInstance(QObject* pParent = nullptr);
	~CSingleInstance() override = default;

	bool Claim();
	void Terminate();

Q_SIGNALS:

	void ShowRequested();

private Q_SLOTS:

	void OnConnection();

private:

	QLocalServer* m_pServer{ nullptr };
	QString       m_name;
};
} // namespace Klip
