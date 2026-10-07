#include "tray_image.hpp"

#include <QtCore/QtEndian>
#include <QtGui/QImage>
#include <QtGui/QPainter>

#include <cstdint>

namespace Klip
{
namespace
{
constexpr int IconSize{ 64 };
constexpr qreal FrameStroke{ 5.0 };
constexpr qreal FrameRadius{ 7.0 };
constexpr qreal DotRadius{ 8.0 };

Desktop::STrayImage DrawImage(bool recording)
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

	Desktop::STrayImage result;
	result.width = image.width();
	result.height = image.height();
	result.pixels.resize(static_cast<size_t>(image.sizeInBytes()));

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
Desktop::STrayIcons DrawTrayIcons()
{
	return Desktop::STrayIcons{ DrawImage(false), DrawImage(true) };
}
} // namespace Klip
