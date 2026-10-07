#pragma once

#include "desktop/request.hpp"
#include "encode/settings.hpp"
#include "recorder/audio_source.hpp"
#include "recorder/recorder.hpp"

#include <tge/non_copyable.hpp>
#include <tge/threading/mpsc_queue.hpp>

#include <QtCore/QString>
#include <QtWidgets/QWidget>

#include <functional>
#include <optional>

class QCheckBox;
class QComboBox;
class QGroupBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QSlider;
class QTimer;

namespace Klip
{
class CLevelMeter;

class CMainWindow final : public QWidget, private Tge::SNoCopyNoMove
{
	Q_OBJECT

public:

	explicit CMainWindow(QWidget* pParent = nullptr);
	~CMainWindow() override = default;

	bool Initialize();
	void Terminate();

	// From any thread: the tray and a second Klip ask through here.
	void Request(Desktop::SRequest const& request);

public Q_SLOTS:

	void Reveal();

protected:

	void closeEvent(QCloseEvent* pEvent) override;

	void showEvent(QShowEvent* pEvent) override;
	void hideEvent(QHideEvent* pEvent) override;

private Q_SLOTS:

	void OnBrowsePressed();
	void OnOpenPressed();
	void OnRecorderUpdate();
	void OnDesktopRequests();

private:

	struct SAudioWidgets final
	{
		QCheckBox*   pEnabled{ nullptr };
		QComboBox*   pDevice{ nullptr };
		QSlider*     pGain{ nullptr };
		QLabel*      pGainValue{ nullptr };
		CLevelMeter* pMeter{ nullptr };
	};

	void BuildLayout();
	void BuildAudioGroup();
	void ConnectInputs();

	// Every change goes through here, so the widgets, Klip.conf and the timer follow whatever it did.
	void Apply(std::function<void()> const& action);
	void AfterChange();
	void Sync();
	bool SyncAudio(Recorder::EAudioSource source, bool idle, bool carries);
	void FitHeight();
	void ArmDeadline();

	void ToggleRecording();
	std::optional<Encode::SRegion> ChooseRegion();
	void ShowInFileManager(QString const& filePath);

	SAudioWidgets& GetAudioWidgets(Recorder::EAudioSource source);

	Recorder::CRecorder m_recorder;

	Tge::Threading::CMpscQueue<Desktop::SRequest> m_requests;

	QComboBox*    m_pSource{ nullptr };
	QLineEdit*    m_pDirectory{ nullptr };
	QPushButton*  m_pBrowse{ nullptr };
	QPushButton*  m_pOpen{ nullptr };
	QComboBox*    m_pContainer{ nullptr };
	QComboBox*    m_pCodec{ nullptr };
	QComboBox*    m_pFrameRate{ nullptr };
	QComboBox*    m_pQuality{ nullptr };
	QLabel*       m_pQualityHint{ nullptr };
	QGroupBox*    m_pAudioGroup{ nullptr };
	QCheckBox*    m_pRememberWindow{ nullptr };
	SAudioWidgets m_system;
	SAudioWidgets m_microphone;
	QComboBox*    m_pAudioQuality{ nullptr };
	QLabel*       m_pAudioQualityHint{ nullptr };
	QPushButton*  m_pRecord{ nullptr };
	QLabel*       m_pElapsed{ nullptr };
	QLabel*       m_pStatus{ nullptr };
	QTimer*       m_pDeadline{ nullptr };
};
} // namespace Klip
