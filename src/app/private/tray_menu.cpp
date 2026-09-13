#include "tray_menu.hpp"

#include "tray_icon.hpp"

#include <QtDBus/QDBusMetaType>

namespace Klip
{
namespace
{
constexpr int RootId{ 0 };
constexpr int ToggleId{ 1 };
constexpr int ShowId{ 2 };
constexpr int SeparatorId{ 3 };
constexpr int QuitId{ 4 };

constexpr int ChildIds[]{ ToggleId, ShowId, SeparatorId, QuitId };
} // namespace

//////////////////////////////////////////////////////////////////////////
QDBusArgument& operator<<(QDBusArgument& argument, SMenuLayout const& layout)
{
	argument.beginStructure();
	argument << layout.id << layout.properties;
	argument.beginArray(qMetaTypeId<QDBusVariant>());

	for (QDBusVariant const& child : layout.children)
	{
		argument << child;
	}

	argument.endArray();
	argument.endStructure();

	return argument;
}

//////////////////////////////////////////////////////////////////////////
QDBusArgument const& operator>>(QDBusArgument const& argument, SMenuLayout& layout)
{
	argument.beginStructure();
	argument >> layout.id >> layout.properties;
	argument.beginArray();

	while (!argument.atEnd())
	{
		QDBusVariant child;
		argument >> child;
		layout.children.append(child);
	}

	argument.endArray();
	argument.endStructure();

	return argument;
}

//////////////////////////////////////////////////////////////////////////
QDBusArgument& operator<<(QDBusArgument& argument, SMenuProperties const& properties)
{
	argument.beginStructure();
	argument << properties.id << properties.properties;
	argument.endStructure();

	return argument;
}

//////////////////////////////////////////////////////////////////////////
QDBusArgument const& operator>>(QDBusArgument const& argument, SMenuProperties& properties)
{
	argument.beginStructure();
	argument >> properties.id >> properties.properties;
	argument.endStructure();

	return argument;
}

//////////////////////////////////////////////////////////////////////////
void RegisterMenuTypes()
{
	qDBusRegisterMetaType<SMenuLayout>();
	qDBusRegisterMetaType<SMenuProperties>();
	qDBusRegisterMetaType<MenuPropertiesList>();
}

//////////////////////////////////////////////////////////////////////////
CTrayMenu::CTrayMenu(CTrayIcon* pOwner, QObject* pParent)
	: QDBusAbstractAdaptor(pParent)
	, m_pOwner(pOwner)
{
	setAutoRelaySignals(false);
}

//////////////////////////////////////////////////////////////////////////
QVariantMap CTrayMenu::PropertiesFor(int id) const
{
	QVariantMap properties;

	switch (id)
	{
		case ToggleId:
			properties["label"] = m_pOwner->IsRecording() ? QStringLiteral("Stop recording")
			                                              : QStringLiteral("Start recording");
			properties["enabled"] = true;
			properties["visible"] = true;
			break;

		case ShowId:
			properties["label"] = QStringLiteral("Show Klip");
			properties["enabled"] = true;
			properties["visible"] = true;
			break;

		case SeparatorId:
			properties["type"] = QStringLiteral("separator");
			properties["visible"] = true;
			break;

		case QuitId:
			properties["label"] = QStringLiteral("Quit");
			properties["enabled"] = true;
			properties["visible"] = true;
			break;

		default:
			properties["children-display"] = QStringLiteral("submenu");
			break;
	}

	return properties;
}

//////////////////////////////////////////////////////////////////////////
uint CTrayMenu::GetLayout(int parentId, int recursionDepth, QStringList const& propertyNames,
                          SMenuLayout& layout)
{
	layout.id = parentId;
	layout.properties = PropertiesFor(parentId);

	if (parentId == RootId && recursionDepth != 0)
	{
		for (int const id : ChildIds)
		{
			SMenuLayout child;
			child.id = id;
			child.properties = PropertiesFor(id);

			layout.children.append(QDBusVariant{ QVariant::fromValue(child) });
		}
	}

	return m_revision;
}

//////////////////////////////////////////////////////////////////////////
MenuPropertiesList CTrayMenu::GetGroupProperties(QList<int> const& ids, QStringList const& propertyNames)
{
	MenuPropertiesList result;

	if (ids.isEmpty())
	{
		for (int const id : ChildIds)
		{
			result.append(SMenuProperties{ id, PropertiesFor(id) });
		}
	}
	else
	{
		for (int const id : ids)
		{
			result.append(SMenuProperties{ id, PropertiesFor(id) });
		}
	}

	return result;
}

//////////////////////////////////////////////////////////////////////////
QDBusVariant CTrayMenu::GetProperty(int id, QString const& name)
{
	return QDBusVariant{ PropertiesFor(id).value(name) };
}

//////////////////////////////////////////////////////////////////////////
void CTrayMenu::Event(int id, QString const& eventId, QDBusVariant const& data, uint timestamp)
{
	if (eventId == QLatin1String("clicked"))
	{
		switch (id)
		{
			case ToggleId: Q_EMIT m_pOwner->ToggleRequested(); break;
			case ShowId: Q_EMIT m_pOwner->ShowRequested(); break;
			case QuitId: Q_EMIT m_pOwner->QuitRequested(); break;
			default: break;
		}
	}
}

//////////////////////////////////////////////////////////////////////////
bool CTrayMenu::AboutToShow(int id)
{
	return false;
}

//////////////////////////////////////////////////////////////////////////
void CTrayMenu::AnnounceChange()
{
	MenuPropertiesList updated;
	updated.append(SMenuProperties{ ToggleId, PropertiesFor(ToggleId) });

	Q_EMIT ItemsPropertiesUpdated(updated, MenuPropertiesList{});
}
} // namespace Klip
