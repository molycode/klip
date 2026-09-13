#pragma once

#include "session.hpp"

#include <capture/audio_devices.hpp>
#include <capture/audio_stream.hpp>

#include <tge/non_copyable.hpp>

#include <QtCore/QElapsedTimer>
#include <QtCore/QString>
#include <QtWidgets/QWidget>

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
class CTrayIcon;

class CMainWindow final : public QWidget, private Tge::SNoCopyNoMove
{
	Q_OBJECT

public:

	explicit CMainWindow(QWidget* pParent = nullptr);
	~CMainWindow() override = default;

	bool Initialize();
	void Terminate();

public Q_SLOTS:

	void Reveal();

public:

protected:

	void closeEvent(QCloseEvent* pEvent) override;

	void showEvent(QShowEvent* pEvent) override;
	void hideEvent(QHideEvent* pEvent) override;

private Q_SLOTS:

	void OnRecordPressed();
	void OnBrowsePressed();
	void OnOpenPressed();
	void OnTick();
	void OnCaptureWithdrawn();
	void OnMeterTick();
	void OnAudioSourceToggled();
	void OnGainChanged();

private:

	void BuildLayout();

	void ConnectSettingSaves();
	void SaveSettings() const;
	void BuildAudioGroup();
	void RefreshCodecs();
	void RefreshSourceOptions();
	void RefreshAudioDevices();
	void RefreshAudioAvailability();
	void RefreshMonitoring();
	void UpdateQualityHint();
	void UpdateAudioQualityHint();
	void StopMonitoring();
	void StartRecording();
	void StopRecording();
	void ShowIdleState(QString const& message);
	void SetInputsEnabled(bool enabled);

	// The meters are outputs, so they are deliberately not part of the set a recording disables.
	void SetAudioInputsEnabled(bool enabled);
	void RefreshMeterVisibility();
	void UpdateMeter(CLevelMeter& meter, Capture::CAudioStream const& stream, uint64_t& numBuffers,
	                 float gain);

	float SystemGain() const;
	float MicrophoneGain() const;

	bool ChooseRegion();
	void ShowInFileManager(QString const& filePath);

	QString OutputDirectory() const;
	QString MakeOutputPath() const;

	QString ContainerLabel(Encode::EContainer container) const;
	QString CodecLabel(Encode::ECodec codec) const;
	QString QualityLabel(Encode::EQuality quality) const;

	bool WantsSystemAudio() const;
	bool WantsMicrophone() const;

	Capture::SAudioDevice CurrentSystemDevice() const;
	Capture::SAudioDevice CurrentMicrophoneDevice() const;
	Encode::EQuality      CurrentAudioQuality() const;

	Encode::EContainer CurrentContainer() const;
	Encode::ECodec     CurrentCodec() const;
	Encode::EQuality   CurrentQuality() const;
	uint32_t           CurrentMaxFrameRate() const;

	CSession    m_session;
	CTrayIcon*  m_pTray{ nullptr };
	QComboBox*  m_pSource{ nullptr };
	QLineEdit*  m_pDirectory{ nullptr };
	QPushButton* m_pBrowse{ nullptr };
	QPushButton* m_pOpen{ nullptr };
	QComboBox*  m_pContainer{ nullptr };
	QComboBox*  m_pCodec{ nullptr };
	QComboBox*  m_pFrameRate{ nullptr };
	QComboBox*  m_pQuality{ nullptr };
	QLabel*     m_pQualityHint{ nullptr };
	QGroupBox*  m_pAudioGroup{ nullptr };
	QCheckBox*  m_pRememberWindow{ nullptr };
	QCheckBox*  m_pSystemEnabled{ nullptr };
	QComboBox*  m_pSystemDevice{ nullptr };
	QCheckBox*  m_pMicrophoneEnabled{ nullptr };
	QComboBox*  m_pMicrophoneDevice{ nullptr };
	QSlider*    m_pSystemGain{ nullptr };
	QLabel*     m_pSystemGainValue{ nullptr };
	QSlider*    m_pMicrophoneGain{ nullptr };
	QLabel*     m_pMicrophoneGainValue{ nullptr };
	QComboBox*  m_pAudioQuality{ nullptr };
	QLabel*     m_pAudioQualityHint{ nullptr };
	CLevelMeter* m_pSystemMeter{ nullptr };
	CLevelMeter* m_pMicrophoneMeter{ nullptr };
	QTimer*     m_pMeterTimer{ nullptr };
	QPushButton* m_pRecord{ nullptr };
	QLabel*     m_pElapsed{ nullptr };
	QLabel*     m_pStatus{ nullptr };
	QTimer*     m_pTimer{ nullptr };

	Capture::CAudioDevices m_audioDevices;
	Capture::CAudioStream  m_systemMonitor;
	Capture::CAudioStream  m_microphoneMonitor;

	QString m_systemMonitored;
	QString m_microphoneMonitored;

	uint64_t m_numSystemBuffers{ 0 };
	uint64_t m_numMicrophoneBuffers{ 0 };

	QElapsedTimer m_clock;
	qint64        m_firstByteMs{ -1 };
	QString         m_currentPath;
	Encode::SRegion m_region;
};
} // namespace Klip
