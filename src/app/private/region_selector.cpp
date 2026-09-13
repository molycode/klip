#include "region_selector.hpp"

#include "log.hpp"
#include "screenshot.hpp"

#include <QtCore/QEventLoop>
#include <QtGui/QGuiApplication>
#include <QtGui/QKeyEvent>
#include <QtGui/QMouseEvent>
#include <QtGui/QPainter>
#include <QtGui/QScreen>

namespace Klip
{
namespace
{
constexpr int MinimumSide{ 16 };
constexpr int LabelMargin{ 8 };
} // namespace

//////////////////////////////////////////////////////////////////////////
CRegionSelector::CRegionSelector(QWidget* pParent)
	: QWidget(pParent)
{
}

//////////////////////////////////////////////////////////////////////////
QRect CRegionSelector::Choose()
{
	setWindowFlags(Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::Tool);
	setAttribute(Qt::WA_TranslucentBackground);
	setCursor(Qt::CrossCursor);
	setMouseTracking(true);

	QScreen const* const pScreen{ QGuiApplication::primaryScreen() };

	if (pScreen != nullptr)
	{
		setGeometry(pScreen->geometry());

		// A fullscreen window is the only way to cover an output, and nothing composites behind one.
		CScreenshot screenshot;
		m_backdrop = screenshot.Take(pScreen->geometry());
	}

	if (m_backdrop.isNull())
	{
		gLog.Error("No desktop screenshot to select over.");
	}

	showFullScreen();
	raise();
	activateWindow();

	QEventLoop loop;
	connect(this, &QObject::destroyed, &loop, &QEventLoop::quit);

	m_accepted = false;
	m_selection = QRect();

	while (isVisible())
	{
		loop.processEvents(QEventLoop::WaitForMoreEvents);
	}

	return m_accepted ? m_selection.normalized() : QRect();
}

//////////////////////////////////////////////////////////////////////////
void CRegionSelector::paintEvent(QPaintEvent* pEvent)
{
	QPainter painter{ this };
	painter.setRenderHint(QPainter::Antialiasing);

	if (!m_backdrop.isNull())
	{
		painter.drawImage(rect(), m_backdrop);
	}

	QRect const selection{ m_selection.normalized() };

	painter.setPen(Qt::NoPen);
	painter.setBrush(QColor{ 0, 0, 0, 110 });

	if (selection.isEmpty())
	{
		painter.drawRect(rect());
	}
	else
	{
		QRegion const shade{ QRegion{ rect() }.subtracted(QRegion{ selection }) };

		for (QRect const& part : shade)
		{
			painter.drawRect(part);
		}

		painter.setBrush(Qt::NoBrush);
		painter.setPen(QPen{ QColor{ 235, 110, 40 }, 2 });
		painter.drawRect(selection.adjusted(0, 0, -1, -1));

		QString const label{ QStringLiteral("%1 x %2").arg(selection.width()).arg(selection.height()) };
		painter.setPen(QColor{ 240, 240, 240 });
		painter.drawText(selection.adjusted(LabelMargin, LabelMargin, 0, 0), Qt::AlignTop | Qt::AlignLeft,
		                 label);
	}
}

//////////////////////////////////////////////////////////////////////////
void CRegionSelector::mousePressEvent(QMouseEvent* pEvent)
{
	if (pEvent->button() == Qt::LeftButton)
	{
		m_origin = pEvent->pos();
		m_selection = QRect{ m_origin, m_origin };
		m_dragging = true;
		update();
	}
}

//////////////////////////////////////////////////////////////////////////
void CRegionSelector::mouseMoveEvent(QMouseEvent* pEvent)
{
	if (m_dragging)
	{
		m_selection = QRect{ m_origin, pEvent->pos() };
		update();
	}
}

//////////////////////////////////////////////////////////////////////////
void CRegionSelector::mouseReleaseEvent(QMouseEvent* pEvent)
{
	if (m_dragging && pEvent->button() == Qt::LeftButton)
	{
		m_dragging = false;
		m_selection = QRect{ m_origin, pEvent->pos() }.normalized();
		m_accepted = m_selection.width() >= MinimumSide && m_selection.height() >= MinimumSide;

		if (m_accepted)
		{
			close();
		}
		else
		{
			m_selection = QRect();
			update();
		}
	}
}

//////////////////////////////////////////////////////////////////////////
void CRegionSelector::keyPressEvent(QKeyEvent* pEvent)
{
	if (pEvent->key() == Qt::Key_Escape)
	{
		m_accepted = false;
		close();
	}
}
} // namespace Klip
