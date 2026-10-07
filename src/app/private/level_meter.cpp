#include "level_meter.hpp"

#include "recorder/decibels.hpp"
#include "recorder/level_ballistics.hpp"

#include <QtGui/QFontMetrics>
#include <QtGui/QLinearGradient>
#include <QtGui/QPainter>

#include <algorithm>

namespace Klip
{
namespace
{
constexpr int BarHeight{ 10 };
constexpr int BarSpacing{ 4 };
constexpr int LabelWidth{ 14 };
constexpr int MarkerWidth{ 2 };

constexpr int      ScaleGap{ 3 };
constexpr int      TickHeight{ 3 };
constexpr float    TickStepDecibels{ 6.0f };
constexpr uint32_t NumTicks{ 10 };
constexpr uint32_t LabelEveryNthTick{ 2 };

float ToPosition(float level)
{
	float const decibels{ std::clamp(Recorder::ToDecibels(level), Recorder::FloorDecibels, 0.0f) };

	return 1.0f - decibels / Recorder::FloorDecibels;
}

QFont ScaleFont(QFont const& base)
{
	QFont font{ base };
	font.setPointSizeF(font.pointSizeF() - 2.0);

	return font;
}

int ScaleHeight(QFont const& base)
{
	return ScaleGap + TickHeight + 1 + QFontMetrics{ ScaleFont(base) }.height();
}

// Even ticks are honest only while the bar stays linear in decibels.
void DrawScale(QPainter& painter, QRect const& bar, int top)
{
	QFontMetrics const metrics{ painter.font() };
	int const          baseline{ top + ScaleGap + TickHeight + 1 + metrics.ascent() };

	painter.setPen(QColor{ 110, 110, 110 });
	painter.setBrush(Qt::NoBrush);

	for (uint32_t step{ 0 }; step <= NumTicks; ++step)
	{
		float const decibels{ Recorder::FloorDecibels + static_cast<float>(step) * TickStepDecibels };
		int const   x{ bar.left() + static_cast<int>(static_cast<float>(bar.width() - 1) *
		                                            (1.0f - decibels / Recorder::FloorDecibels)) };

		painter.drawLine(x, top + ScaleGap, x, top + ScaleGap + TickHeight);

		if (step % LabelEveryNthTick == 0)
		{
			QString const text{ step == NumTicks ? QStringLiteral("0 dBFS")
			                                     : QString::number(static_cast<int>(decibels)) };
			int const     textWidth{ metrics.horizontalAdvance(text) };
			int           textX{ x - textWidth / 2 };

			if (step == 0)
			{
				textX = x;
			}
			else if (step == NumTicks)
			{
				textX = x - textWidth;
			}

			painter.drawText(QPoint{ textX, baseline }, text);
		}
	}
}

// Anchored to the scale rather than to the current level, so a colour always means the same loudness.
QLinearGradient MakeGradient(QRect const& bar)
{
	QLinearGradient gradient{ QPointF{ static_cast<qreal>(bar.left()), 0.0 },
		                      QPointF{ static_cast<qreal>(bar.right()), 0.0 } };

	gradient.setColorAt(0.00, QColor{ 60, 185, 105 });
	gradient.setColorAt(0.62, QColor{ 105, 205, 110 });
	gradient.setColorAt(0.74, QColor{ 225, 190, 75 });
	gradient.setColorAt(0.88, QColor{ 230, 140, 60 });
	gradient.setColorAt(1.00, QColor{ 222, 60, 52 });

	return gradient;
}
} // namespace

//////////////////////////////////////////////////////////////////////////
CLevelMeter::CLevelMeter(QWidget* pParent)
	: QWidget{ pParent }
{
	SetChannelCount(2);
}

//////////////////////////////////////////////////////////////////////////
void CLevelMeter::SetChannelCount(uint32_t numChannels)
{
	m_numChannels = std::min(numChannels, MaxChannels);

	// A form row will otherwise squeeze the widget below its hint and silently drop a channel.
	setFixedHeight(sizeHint().height());

	updateGeometry();
	update();
}

//////////////////////////////////////////////////////////////////////////
void CLevelMeter::SetBallistics(Recorder::CLevelBallistics const* pBallistics)
{
	m_pBallistics = pBallistics;

	update();
}

//////////////////////////////////////////////////////////////////////////
QSize CLevelMeter::sizeHint() const
{
	int const height{ static_cast<int>(m_numChannels) * (BarHeight + BarSpacing) - BarSpacing };

	return QSize{ 160, std::max(height, BarHeight) + ScaleHeight(font()) };
}

//////////////////////////////////////////////////////////////////////////
void CLevelMeter::paintEvent(QPaintEvent* pEvent)
{
	QPainter painter{ this };
	painter.setRenderHint(QPainter::Antialiasing, false);

	QFont labelFont{ font() };
	labelFont.setPointSizeF(labelFont.pointSizeF() - 1.0);
	painter.setFont(labelFont);

	if (m_pBallistics != nullptr && m_pBallistics->IsUnavailable())
	{
		painter.setPen(QColor{ 222, 120, 100 });
		painter.drawText(rect(), Qt::AlignLeft | Qt::AlignVCenter,
		                 tr("unavailable — pick another device"));
	}
	else
	{
		for (uint32_t index{ 0 }; index < m_numChannels; ++index)
		{
			float const level{ m_pBallistics != nullptr ? m_pBallistics->GetLevel(index) : 0.0f };
			float const hold{ m_pBallistics != nullptr ? m_pBallistics->GetHold(index) : 0.0f };
			bool const  clipped{ m_pBallistics != nullptr && m_pBallistics->IsClipped(index) };

			int const   top{ static_cast<int>(index) * (BarHeight + BarSpacing) };
			QRect const bar{ LabelWidth, top, width() - LabelWidth, BarHeight };

			painter.setPen(QColor{ 140, 140, 140 });
			painter.drawText(QRect{ 0, top, LabelWidth, BarHeight }, Qt::AlignLeft | Qt::AlignVCenter,
			                 m_numChannels == 2 ? (index == 0 ? QStringLiteral("L") : QStringLiteral("R"))
			                                    : QString::number(index + 1));

			painter.setPen(Qt::NoPen);
			painter.setBrush(QColor{ 40, 40, 40 });
			painter.drawRect(bar);

			QLinearGradient const gradient{ MakeGradient(bar) };

			int const filled{ static_cast<int>(static_cast<float>(bar.width()) * ToPosition(level)) };

			if (filled > 0)
			{
				painter.setBrush(gradient);
				painter.drawRect(QRect{ bar.left(), bar.top(), filled, bar.height() });
			}

			int const marker{ std::min(static_cast<int>(static_cast<float>(bar.width()) *
			                                           ToPosition(hold)),
			                           bar.width() - MarkerWidth) };

			// Only while it is ahead of the bar: a steady signal holds its own peak, and a marker
			// sitting on the tip marks nothing -- on a quiet input it is all there is to see.
			if (marker > filled + MarkerWidth)
			{
				painter.setBrush(QColor{ 235, 235, 235 });
				painter.drawRect(QRect{ bar.left() + marker, bar.top(), MarkerWidth, bar.height() });
			}

			if (clipped)
			{
				painter.setBrush(QColor{ 240, 60, 50 });
				painter.drawRect(QRect{ bar.right() - MarkerWidth, bar.top(), MarkerWidth,
				                        bar.height() });
			}
		}

		if (m_numChannels > 0)
		{
			int const   top{ static_cast<int>(m_numChannels) * (BarHeight + BarSpacing) - BarSpacing };
			QRect const bar{ LabelWidth, 0, width() - LabelWidth, BarHeight };

			painter.setFont(ScaleFont(font()));
			DrawScale(painter, bar, top);
		}
	}
}
} // namespace Klip
