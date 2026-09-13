#pragma once

#include <tge/non_copyable.hpp>

#include <QtCore/QPoint>
#include <QtCore/QRect>
#include <QtGui/QImage>
#include <QtWidgets/QWidget>

namespace Klip
{
class CRegionSelector final : public QWidget, private Tge::SNoCopyNoMove
{
	Q_OBJECT

public:

	explicit CRegionSelector(QWidget* pParent = nullptr);
	~CRegionSelector() override = default;

	// Blocks on a nested event loop until the user answers.
	QRect Choose();

protected:

	void paintEvent(QPaintEvent* pEvent) override;
	void mousePressEvent(QMouseEvent* pEvent) override;
	void mouseMoveEvent(QMouseEvent* pEvent) override;
	void mouseReleaseEvent(QMouseEvent* pEvent) override;
	void keyPressEvent(QKeyEvent* pEvent) override;

private:

	QImage m_backdrop;
	QPoint m_origin;
	QRect  m_selection;
	bool   m_dragging{ false };
	bool   m_accepted{ false };
};
} // namespace Klip
