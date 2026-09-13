#pragma once

#include "tray_types.hpp"

#include <tge/non_copyable.hpp>

#include <QtCore/QObject>
#include <QtCore/QString>

namespace Klip
{
class CTrayAdaptor;
class CTrayMenu;

class CTrayIcon final : public QObject, private Tge::SNoCopyNoMove
{
	Q_OBJECT

public:

	explicit CTrayIcon(QObject* pParent = nullptr);
	~CTrayIcon() override = default;

	bool Initialize();
	void Terminate();

	bool IsAvailable() const { return m_registered; }
	bool IsRecording() const { return m_recording; }

	void SetRecording(bool recording);

	void SetLabel(QString const& label);
	void SetDetail(QString const& detail);

	TrayImageList IconImages() const;
	QString       TitleText() const;
	QString       LabelText() const { return m_label; }
	QString       DetailText() const { return m_detail; }

Q_SIGNALS:

	void ToggleRequested();
	void ShowRequested();
	void QuitRequested();

private:

	CTrayAdaptor* m_pAdaptor{ nullptr };
	CTrayMenu*    m_pMenu{ nullptr };
	QObject*      m_pMenuObject{ nullptr };
	QString       m_serviceName;
	QString       m_label{ QStringLiteral("Klip") };
	QString       m_detail;
	bool          m_recording{ false };
	bool          m_registered{ false };
};
} // namespace Klip
