#pragma once

#include "tray_types.hpp"

#include <QtCore/QString>
#include <QtDBus/QDBusAbstractAdaptor>
#include <QtDBus/QDBusObjectPath>

namespace Klip
{
class CTrayIcon;

// org.kde.StatusNotifierItem. Implemented here rather than through QSystemTrayIcon because only this
// interface carries XAyatanaLabel, the text a panel shows beside the icon.
class CTrayAdaptor final : public QDBusAbstractAdaptor
{
	Q_OBJECT
	Q_CLASSINFO("D-Bus Interface", "org.kde.StatusNotifierItem")

	Q_PROPERTY(QString Category READ Category)
	Q_PROPERTY(QString Id READ Id)
	Q_PROPERTY(QString Title READ Title)
	Q_PROPERTY(QString Status READ Status)
	Q_PROPERTY(QString IconName READ IconName)
	Q_PROPERTY(Klip::TrayImageList IconPixmap READ IconPixmap)
	Q_PROPERTY(QString AttentionIconName READ AttentionIconName)
	Q_PROPERTY(QString OverlayIconName READ OverlayIconName)
	Q_PROPERTY(Klip::STrayToolTip ToolTip READ ToolTip)
	Q_PROPERTY(QDBusObjectPath Menu READ Menu)
	Q_PROPERTY(bool ItemIsMenu READ ItemIsMenu)
	Q_PROPERTY(QString XAyatanaLabel READ XAyatanaLabel)
	Q_PROPERTY(QString XAyatanaLabelGuide READ XAyatanaLabelGuide)

public:

	explicit CTrayAdaptor(CTrayIcon* pOwner);
	~CTrayAdaptor() override = default;

	QString            Category() const { return QStringLiteral("ApplicationStatus"); }
	QString            Id() const { return QStringLiteral("klip"); }
	QString            Title() const;
	QString            Status() const;
	QString            IconName() const { return QString{}; }
	TrayImageList      IconPixmap() const;
	QString            AttentionIconName() const { return QString{}; }
	QString            OverlayIconName() const { return QString{}; }
	STrayToolTip       ToolTip() const;
	QDBusObjectPath    Menu() const;
	bool               ItemIsMenu() const { return false; }
	QString            XAyatanaLabel() const;
	QString            XAyatanaLabelGuide() const;

public Q_SLOTS:

	void Activate(int x, int y);
	void ProvideXdgActivationToken(QString const& token);
	void SecondaryActivate(int x, int y);
	void ContextMenu(int x, int y);
	void Scroll(int delta, QString const& orientation);

Q_SIGNALS:

	void NewIcon();
	void NewTitle();
	void NewToolTip();
	void NewStatus(QString const& status);
	void XAyatanaNewLabel(QString const& label, QString const& guide);

private:

	CTrayIcon* m_pOwner{ nullptr };
};
} // namespace Klip
