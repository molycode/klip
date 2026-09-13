#include "tray_adaptor.hpp"

#include "tray_icon.hpp"

#include <QtCore/QtGlobal>

namespace Klip
{
//////////////////////////////////////////////////////////////////////////
CTrayAdaptor::CTrayAdaptor(CTrayIcon* pOwner)
	: QDBusAbstractAdaptor(pOwner)
	, m_pOwner(pOwner)
{
	setAutoRelaySignals(false);
}

//////////////////////////////////////////////////////////////////////////
QString CTrayAdaptor::Title() const
{
	return m_pOwner->TitleText();
}

//////////////////////////////////////////////////////////////////////////
QString CTrayAdaptor::Status() const
{
	return QStringLiteral("Active");
}

//////////////////////////////////////////////////////////////////////////
TrayImageList CTrayAdaptor::IconPixmap() const
{
	return m_pOwner->IconImages();
}

//////////////////////////////////////////////////////////////////////////
STrayToolTip CTrayAdaptor::ToolTip() const
{
	STrayToolTip toolTip;
	toolTip.title       = m_pOwner->TitleText();
	toolTip.description = m_pOwner->DetailText();

	return toolTip;
}

//////////////////////////////////////////////////////////////////////////
QDBusObjectPath CTrayAdaptor::Menu() const
{
	return QDBusObjectPath{ QStringLiteral("/MenuBar") };
}

//////////////////////////////////////////////////////////////////////////
QString CTrayAdaptor::XAyatanaLabel() const
{
	return m_pOwner->LabelText();
}

//////////////////////////////////////////////////////////////////////////
QString CTrayAdaptor::XAyatanaLabelGuide() const
{
	// The widest text the label will ever hold, so the panel reserves room and stops twitching.
	return QStringLiteral("00:00:00 \u00b7 999.9 GiB/h");
}

//////////////////////////////////////////////////////////////////////////
void CTrayAdaptor::Activate(int x, int y)
{
	Q_EMIT m_pOwner->ShowRequested();
}

//////////////////////////////////////////////////////////////////////////
void CTrayAdaptor::ProvideXdgActivationToken(QString const& token)
{
	// The compositor refuses a raise it did not sanction, and Qt's Wayland plugin looks here for
	// the token that sanctions this one.
	qputenv("XDG_ACTIVATION_TOKEN", token.toUtf8());
}

//////////////////////////////////////////////////////////////////////////
void CTrayAdaptor::SecondaryActivate(int x, int y)
{
	Q_EMIT m_pOwner->ToggleRequested();
}

//////////////////////////////////////////////////////////////////////////
void CTrayAdaptor::ContextMenu(int x, int y)
{
}

//////////////////////////////////////////////////////////////////////////
void CTrayAdaptor::Scroll(int delta, QString const& orientation)
{
}
} // namespace Klip
