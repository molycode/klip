#pragma once

#include <QtCore/QByteArray>
#include <QtCore/QList>
#include <QtCore/QMetaType>
#include <QtDBus/QDBusArgument>

namespace Klip
{
// (iiay) as the StatusNotifierItem specification marshals an icon: ARGB32, network byte order.
struct STrayImage final
{
	int        width{ 0 };
	int        height{ 0 };
	QByteArray pixels;
};

using TrayImageList = QList<STrayImage>;

// (sa(iiay)ss)
struct STrayToolTip final
{
	QString       iconName;
	TrayImageList iconPixmaps;
	QString       title;
	QString       description;
};

QDBusArgument& operator<<(QDBusArgument& argument, STrayImage const& image);
QDBusArgument const& operator>>(QDBusArgument const& argument, STrayImage& image);
QDBusArgument& operator<<(QDBusArgument& argument, STrayToolTip const& toolTip);
QDBusArgument const& operator>>(QDBusArgument const& argument, STrayToolTip& toolTip);

void RegisterTrayTypes();
} // namespace Klip

Q_DECLARE_METATYPE(Klip::STrayImage)
Q_DECLARE_METATYPE(Klip::TrayImageList)
Q_DECLARE_METATYPE(Klip::STrayToolTip)
