#pragma once

#include <tge/non_copyable.hpp>

#include <QtCore/QObject>
#include <QtGui/QImage>

#include <string>

class QEventLoop;
class QRect;

struct sd_bus_message;
struct sd_bus_slot;

namespace Klip
{
// A QObject only so the bus thread can wake the nested loop through Qt's queue.
class CScreenshot final : public QObject, private Tge::SNoCopyNoMove
{
public:

	CScreenshot() = default;
	~CScreenshot() override = default;

	// Blocks on a nested event loop until the portal answers.
	QImage Take(QRect const& area);

private:

	void OnResponse(sd_bus_message* pMessage);

	// Written on the bus thread, and read here only after the unsubscribe that ends Take.
	std::string m_uri;

	sd_bus_slot* m_pSlot{ nullptr };
	QEventLoop*  m_pLoop{ nullptr };
};
} // namespace Klip
