#pragma once

#include <tge/non_copyable.hpp>

#include <QtCore/QObject>
#include <QtCore/QString>
#include <QtCore/QVariantMap>
#include <QtGui/QImage>

class QEventLoop;
class QRect;

namespace Klip
{
class CScreenshot final : public QObject, private Tge::SNoCopyNoMove
{
	Q_OBJECT

public:

	explicit CScreenshot(QObject* pParent = nullptr);
	~CScreenshot() override = default;

	// Blocks on a nested event loop until the portal answers.
	QImage Take(QRect const& area);

private Q_SLOTS:

	// uint, not uint32_t: QDBusConnection matches on the signature as written.
	void OnResponse(uint response, QVariantMap const& results);

private:

	QString     m_requestPath;
	QString     m_uri;
	QEventLoop* m_pLoop{ nullptr };
};
} // namespace Klip
