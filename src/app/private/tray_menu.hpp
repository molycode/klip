#pragma once

#include <QtCore/QList>
#include <QtCore/QMetaType>
#include <QtCore/QVariantMap>
#include <QtDBus/QDBusAbstractAdaptor>
#include <QtDBus/QDBusArgument>
#include <QtDBus/QDBusVariant>

namespace Klip
{
class CTrayIcon;

// (ia{sv}av), where each child variant holds another of these.
struct SMenuLayout final
{
	int                 id{ 0 };
	QVariantMap         properties;
	QList<QDBusVariant> children;
};

// (ia{sv}) for property updates
struct SMenuProperties final
{
	int         id{ 0 };
	QVariantMap properties;
};

using MenuPropertiesList = QList<SMenuProperties>;

QDBusArgument& operator<<(QDBusArgument& argument, SMenuLayout const& layout);
QDBusArgument const& operator>>(QDBusArgument const& argument, SMenuLayout& layout);
QDBusArgument& operator<<(QDBusArgument& argument, SMenuProperties const& properties);
QDBusArgument const& operator>>(QDBusArgument const& argument, SMenuProperties& properties);

void RegisterMenuTypes();

// com.canonical.dbusmenu. A StatusNotifierItem's menu is this protocol and nothing else, so choosing to
// provide the item means providing this too.
class CTrayMenu final : public QDBusAbstractAdaptor
{
	Q_OBJECT
	Q_CLASSINFO("D-Bus Interface", "com.canonical.dbusmenu")

	Q_PROPERTY(uint Version READ Version)
	Q_PROPERTY(QString TextDirection READ TextDirection)
	Q_PROPERTY(QString Status READ Status)
	Q_PROPERTY(QStringList IconThemePath READ IconThemePath)

public:

	CTrayMenu(CTrayIcon* pOwner, QObject* pParent);
	~CTrayMenu() override = default;

	uint        Version() const { return 3; }
	QString     TextDirection() const { return QStringLiteral("ltr"); }
	QString     Status() const { return QStringLiteral("normal"); }
	QStringList IconThemePath() const { return QStringList{}; }

	void AnnounceChange();

public Q_SLOTS:

	Q_NOREPLY void Event(int id, QString const& eventId, QDBusVariant const& data, uint timestamp);
	bool AboutToShow(int id);
	// Qt resolves an output type by name, and a bare SMenuLayout& silently drops the method.
	uint GetLayout(int parentId, int recursionDepth, QStringList const& propertyNames,
	               Klip::SMenuLayout& layout);
	MenuPropertiesList GetGroupProperties(QList<int> const& ids, QStringList const& propertyNames);
	QDBusVariant GetProperty(int id, QString const& name);

Q_SIGNALS:

	void ItemsPropertiesUpdated(Klip::MenuPropertiesList updated, Klip::MenuPropertiesList removed);
	void LayoutUpdated(uint revision, int parent);
	void ItemActivationRequested(int id, uint timestamp);

private:

	QVariantMap PropertiesFor(int id) const;

	CTrayIcon* m_pOwner{ nullptr };
	uint       m_revision{ 1 };
};
} // namespace Klip

Q_DECLARE_METATYPE(Klip::SMenuLayout)
Q_DECLARE_METATYPE(Klip::SMenuProperties)
Q_DECLARE_METATYPE(Klip::MenuPropertiesList)
