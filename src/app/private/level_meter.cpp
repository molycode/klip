#include "level_meter.hpp"

#include <QtGui/QFontMetrics>
#include <QtGui/QLinearGradient>
#include <QtGui/QPainter>

#include <algorithm>
#include <cmath>

namespace Klip
{
namespace
{
constexpr float FloorDecibels{ -60.0f };

constexpr int   HoldMilliseconds{ 1500 };
constexpr float HoldFallPerSecond{ 20.0f };
constexpr float LevelFallPerSecond{ 60.0f };

constexpr int ClipMilliseconds{ 3000 };

constexpr int BarHeight{ 10 };
constexpr int BarSpacing{ 4 };
constexpr int LabelWidth{ 14 };
constexpr int MarkerWidth{ 2 };

constexpr int      ScaleGap{ 3 };
constexpr int      TickHeight{ 3 };
constexpr float    TickStepDecibels{ 6.0f };
constexpr uint32_t NumTicks{ 10 };
constexpr uint32_t LabelEveryNthTick{ 2 };

float ToDecibels(float level)
{
	return level > 0.0f ? 20.0f * std::log10(level) : FloorDecibels;
}

float FromDecibels(float decibels)
{
	return decibels <= FloorDecibels ? 0.0f : std::pow(10.0f, decibels / 20.0f);
}

float ToPosition(float level)
{
	float const decibels{ std::clamp(ToDecibels(level), FloorDecibels, 0.0f) };

	return 1.0f - decibels / FloorDecibels;
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
		float const decibels{ FloorDecibels + static_cast<float>(step) * TickStepDecibels };
		int const   x{ bar.left() + static_cast<int>(static_cast<float>(bar.width() - 1) *
		                                            (1.0f - decibels / FloorDecibels)) };

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
void CLevelMeter::SetPeaks(float const* pPeaks, uint32_t numChannels)
{
	uint32_t const channels{ std::min(numChannels, m_numChannels) };

	for (uint32_t index{ 0 }; index < channels; ++index)
	{
		SChannel&   channel{ m_channels[index] };
		float const peak{ pPeaks != nullptr ? pPeaks[index] : 0.0f };

		// Buffers arrive in bursts, so a bar driven straight off the last one flickers and reads low.
		float const elapsed{ channel.fallingSince.isValid()
			                     ? static_cast<float>(channel.fallingSince.elapsed()) / 1000.0f
			                     : 0.0f };
		float const fallen{ FromDecibels(ToDecibels(channel.level) - LevelFallPerSecond * elapsed) };

		channel.level = std::max(peak, fallen);
		channel.fallingSince.restart();

		if (peak >= 1.0f)
		{
			channel.clipped = true;
			channel.clippedSince.restart();
		}
		else if (channel.clipped && channel.clippedSince.elapsed() > ClipMilliseconds)
		{
			channel.clipped = false;
		}

		if (peak >= channel.hold || !channel.heldSince.isValid())
		{
			channel.hold = peak;
			channel.heldSince.restart();
		}
		else if (channel.heldSince.elapsed() > HoldMilliseconds)
		{
			float const holding{ static_cast<float>(channel.heldSince.elapsed() - HoldMilliseconds) /
				                 1000.0f };

			channel.hold = std::max(peak, FromDecibels(ToDecibels(channel.hold) -
			                                           HoldFallPerSecond * holding));
		}
	}

	update();
}

//////////////////////////////////////////////////////////////////////////
void CLevelMeter::Reset()
{
	for (SChannel& channel : m_channels)
	{
		channel = SChannel{};
	}

	update();
}

//////////////////////////////////////////////////////////////////////////
void CLevelMeter::SetUnavailable(bool unavailable)
{
	m_unavailable = unavailable;

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

	if (m_unavailable)
	{
		painter.setPen(QColor{ 222, 120, 100 });
		painter.drawText(rect(), Qt::AlignLeft | Qt::AlignVCenter,
		                 tr("unavailable — pick another device"));
	}
	else
	{
		for (uint32_t index{ 0 }; index < m_numChannels; ++index)
		{
			SChannel const& channel{ m_channels[index] };

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

			int const filled{ static_cast<int>(static_cast<float>(bar.width()) * ToPosition(channel.level)) };

			if (filled > 0)
			{
				painter.setBrush(gradient);
				painter.drawRect(QRect{ bar.left(), bar.top(), filled, bar.height() });
			}

			int const marker{ std::min(static_cast<int>(static_cast<float>(bar.width()) *
			                                           ToPosition(channel.hold)),
			                           bar.width() - MarkerWidth) };

			// Only while it is ahead of the bar: a steady signal holds its own peak, and a marker
			// sitting on the tip marks nothing -- on a quiet input it is all there is to see.
			if (marker > filled + MarkerWidth)
			{
				painter.setBrush(QColor{ 235, 235, 235 });
				painter.drawRect(QRect{ bar.left() + marker, bar.top(), MarkerWidth, bar.height() });
			}

			if (channel.clipped)
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
