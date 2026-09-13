#include "tray_types.hpp"

#include <QtDBus/QDBusMetaType>

namespace Klip
{
//////////////////////////////////////////////////////////////////////////
QDBusArgument& operator<<(QDBusArgument& argument, STrayImage const& image)
{
	argument.beginStructure();
	argument << image.width << image.height << image.pixels;
	argument.endStructure();

	return argument;
}

//////////////////////////////////////////////////////////////////////////
QDBusArgument const& operator>>(QDBusArgument const& argument, STrayImage& image)
{
	argument.beginStructure();
	argument >> image.width >> image.height >> image.pixels;
	argument.endStructure();

	return argument;
}

//////////////////////////////////////////////////////////////////////////
QDBusArgument& operator<<(QDBusArgument& argument, STrayToolTip const& toolTip)
{
	argument.beginStructure();
	argument << toolTip.iconName << toolTip.iconPixmaps << toolTip.title << toolTip.description;
	argument.endStructure();

	return argument;
}

//////////////////////////////////////////////////////////////////////////
QDBusArgument const& operator>>(QDBusArgument const& argument, STrayToolTip& toolTip)
{
	argument.beginStructure();
	argument >> toolTip.iconName >> toolTip.iconPixmaps >> toolTip.title >> toolTip.description;
	argument.endStructure();

	return argument;
}

//////////////////////////////////////////////////////////////////////////
void RegisterTrayTypes()
{
	qDBusRegisterMetaType<STrayImage>();
	qDBusRegisterMetaType<TrayImageList>();
	qDBusRegisterMetaType<STrayToolTip>();
}
} // namespace Klip
