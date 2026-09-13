#include "main_window.hpp"

#include "level_meter.hpp"
#include "log.hpp"
#include "region_selector.hpp"
#include "tray_icon.hpp"

#include <encode/capabilities.hpp>

#include <QtCore/QDateTime>
#include <QtCore/QEventLoop>
#include <QtCore/QMetaObject>
#include <QtCore/QDir>
#include <QtCore/QFileInfo>
#include <QtCore/QSettings>
#include <QtCore/QSignalBlocker>
#include <QtCore/QStandardPaths>
#include <QtCore/QTimer>
#include <QtCore/QUrl>
#include <QtDBus/QDBusConnection>
#include <QtDBus/QDBusInterface>
#include <QtDBus/QDBusMessage>
#include <QtGui/QCloseEvent>
#include <QtGui/QHideEvent>
#include <QtGui/QShowEvent>
#include <QtGui/QGuiApplication>
#include <QtGui/QScreen>
#include <QtGui/QDesktopServices>
#include <QtWidgets/QApplication>
#include <QtWidgets/QCheckBox>
#include <QtWidgets/QComboBox>
#include <QtWidgets/QFileDialog>
#include <QtWidgets/QFormLayout>
#include <QtWidgets/QGroupBox>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QLabel>
#include <QtWidgets/QLayout>
#include <QtWidgets/QLineEdit>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QSlider>
#include <QtWidgets/QVBoxLayout>
#include <tge/profiling/profiling.hpp>

#include <cmath>

namespace Klip
{
namespace
{
constexpr int      TickMilliseconds{ 250 };
constexpr qint64   RateSettleSeconds{ 3 };
constexpr int      MeterMilliseconds{ 40 };
constexpr int      GainValueWidth{ 52 };
constexpr int      WindowWidth{ 600 };

constexpr char const* RecordButtonStyle{
	"QPushButton { border: 1px solid rgba(235, 110, 40, 0.55); border-radius: 4px; }"
	"QPushButton:hover { border-color: rgba(235, 110, 40, 0.9);"
	"                    background-color: rgba(235, 110, 40, 0.10); }"
	"QPushButton:pressed { background-color: rgba(235, 110, 40, 0.18); }"
	"QPushButton:disabled { border-color: rgba(140, 140, 140, 0.35); }"
};

constexpr uint32_t FallbackFrameRate{ 60 };

constexpr int NoFrameRateCap{ 0 };

// GNOME animates a window away over about 150 ms, and until it is gone it is still on screen: in the
// selector's backdrop, and in the first frames of a capture that starts behind it.
constexpr int      HideSettleMilliseconds{ 250 };

constexpr char const* DirectoryKey{ "output/directory" };
constexpr char const* ContainerKey{ "output/container" };
constexpr char const* CodecKey{ "output/codec" };
constexpr char const* QualityKey{ "output/quality" };
constexpr char const* SourceKey{ "capture/source" };
constexpr char const* FrameRateKey{ "capture/frameRate" };
constexpr char const* RememberWindowKey{ "capture/rememberWindow" };
constexpr char const* SystemAudioKey{ "audio/systemEnabled" };
constexpr char const* SystemDeviceKey{ "audio/systemDevice" };
constexpr char const* MicrophoneKey{ "audio/microphoneEnabled" };
constexpr char const* MicrophoneDeviceKey{ "audio/microphoneDevice" };
constexpr char const* AudioQualityKey{ "audio/quality" };
constexpr char const* SystemGainKey{ "audio/systemGain" };
constexpr char const* MicrophoneGainKey{ "audio/microphoneGain" };

constexpr int MinimumGainDecibels{ -30 };
constexpr int MaximumGainDecibels{ 20 };

float FromDecibels(int decibels)
{
	return std::pow(10.0f, static_cast<float>(decibels) / 20.0f);
}

constexpr int SourceScreen{ 0 };
constexpr int SourceWindow{ 1 };
constexpr int SourceRegion{ 2 };

QString ToQString(std::string_view text)
{
	return QString::fromUtf8(text.data(), static_cast<qsizetype>(text.size()));
}

//////////////////////////////////////////////////////////////////////////
void SettleAfterHiding()
{
	TGE_PROFILE_SCOPE_N("Wait: hide settle");

	QEventLoop settle;
	QTimer::singleShot(HideSettleMilliseconds, &settle, &QEventLoop::quit);
	settle.exec();
}

//////////////////////////////////////////////////////////////////////////
QString FormatDuration(qint64 milliseconds)
{
	qint64 const totalSeconds{ milliseconds / 1000 };

	return QStringLiteral("%1:%2:%3")
		.arg(totalSeconds / 3600, 2, 10, QLatin1Char('0'))
		.arg((totalSeconds / 60) % 60, 2, 10, QLatin1Char('0'))
		.arg(totalSeconds % 60, 2, 10, QLatin1Char('0'));
}

QString FormatBytes(uint64_t bytes)
{
	double const mebibytes{ static_cast<double>(bytes) / (1024.0 * 1024.0) };

	return QStringLiteral("%1 MiB").arg(mebibytes, 0, 'f', 1);
}

struct SThroughput final
{
	QString perMinute;
	QString perHour;
};

SThroughput FormatThroughput(uint64_t bytes, qint64 elapsedSeconds)
{
	SThroughput result;

	if (elapsedSeconds >= RateSettleSeconds)
	{
		double const perMinute{ static_cast<double>(bytes) / (1024.0 * 1024.0) * 60.0
		                        / static_cast<double>(elapsedSeconds) };
		double const perHour{ perMinute * 60.0 };

		result.perMinute = QStringLiteral("%1 MiB/min").arg(perMinute, 0, 'f', 1);
		result.perHour   = perHour >= 1024.0
			                   ? QStringLiteral("%1 GiB/h").arg(perHour / 1024.0, 0, 'f', 1)
			                   : QStringLiteral("%1 MiB/h").arg(perHour, 0, 'f', 0);
	}

	return result;
}
} // namespace

//////////////////////////////////////////////////////////////////////////
CMainWindow::CMainWindow(QWidget* pParent)
	: QWidget(pParent)
{
}

//////////////////////////////////////////////////////////////////////////
void CMainWindow::BuildLayout()
{
	setWindowTitle(tr("Klip %1").arg(QStringLiteral(KLIP_VERSION)));
	setFixedWidth(WindowWidth);

	m_pSource = new QComboBox(this);
	m_pSource->addItem(tr("Whole screen"), SourceScreen);
	m_pSource->addItem(tr("A window"), SourceWindow);
	m_pSource->addItem(tr("Part of the screen"), SourceRegion);

	m_pRememberWindow = new QCheckBox(tr("Remember the window"), this);
	m_pRememberWindow->setToolTip(
		tr("Skip the picker and record the same window again. Untick to be asked each time."));

	m_pDirectory = new QLineEdit(this);
	m_pDirectory->setReadOnly(true);
	m_pDirectory->setText(OutputDirectory());

	m_pBrowse = new QPushButton(tr("Change…"), this);
	connect(m_pBrowse, &QPushButton::clicked, this, &CMainWindow::OnBrowsePressed);

	m_pOpen = new QPushButton(tr("Open"), this);
	m_pOpen->setToolTip(tr("Show the recordings in your file manager"));
	connect(m_pOpen, &QPushButton::clicked, this, &CMainWindow::OnOpenPressed);

	QHBoxLayout* const pDirectoryRow{ new QHBoxLayout };
	pDirectoryRow->addWidget(m_pDirectory, 1);
	pDirectoryRow->addWidget(m_pOpen);
	pDirectoryRow->addWidget(m_pBrowse);

	QSettings settings;

	Encode::EContainer const savedContainer{ Encode::ParseContainer(
		settings.value(ContainerKey, QStringLiteral("mp4")).toString().toStdString(),
		Encode::EContainer::Mp4) };

	m_pContainer = new QComboBox(this);

	for (size_t index{ 0 }; index < Encode::ContainerCount; ++index)
	{
		Encode::EContainer const container{ static_cast<Encode::EContainer>(index) };

		bool usable{ false };

		for (size_t codecIndex{ 0 }; codecIndex < Encode::CodecCount; ++codecIndex)
		{
			Encode::ECodec const codec{ static_cast<Encode::ECodec>(codecIndex) };

			usable = usable || (Encode::ContainerAccepts(container, codec) &&
			                    Encode::IsCodecOffered(codec));
		}

		if (usable)
		{
			m_pContainer->addItem(ContainerLabel(container), static_cast<int>(container));
		}
	}

	int const containerIndex{ m_pContainer->findData(static_cast<int>(savedContainer)) };
	m_pContainer->setCurrentIndex(containerIndex >= 0 ? containerIndex : 0);

	m_pCodec = new QComboBox(this);
	RefreshCodecs();

	Encode::ECodec const savedCodec{ Encode::ParseCodec(
		settings.value(CodecKey, QStringLiteral("h264")).toString().toStdString(),
		Encode::ECodec::H264) };

	int const codecIndex{ m_pCodec->findData(static_cast<int>(savedCodec)) };

	if (codecIndex >= 0)
	{
		m_pCodec->setCurrentIndex(codecIndex);
	}

	connect(m_pContainer, &QComboBox::currentIndexChanged, this, &CMainWindow::RefreshCodecs);

	m_pFrameRate = new QComboBox(this);
	m_pFrameRate->addItem(tr("Up to 30 fps"), 30);
	m_pFrameRate->addItem(tr("Up to 60 fps"), 60);
	m_pFrameRate->addItem(tr("Uncapped"), NoFrameRateCap);

	int const savedFrameRate{ settings.value(FrameRateKey, NoFrameRateCap).toInt() };
	int const frameRateIndex{ m_pFrameRate->findData(savedFrameRate) };
	m_pFrameRate->setCurrentIndex(frameRateIndex >= 0 ? frameRateIndex : 2);

	m_pQuality = new QComboBox(this);

	for (size_t index{ 0 }; index < Encode::QualityCount; ++index)
	{
		Encode::EQuality const quality{ static_cast<Encode::EQuality>(index) };
		m_pQuality->addItem(QualityLabel(quality), static_cast<int>(quality));
	}

	Encode::EQuality const savedQuality{ Encode::ParseQuality(
		settings.value(QualityKey, QStringLiteral("balanced")).toString().toStdString(),
		Encode::EQuality::Balanced) };

	m_pQuality->setCurrentIndex(m_pQuality->findData(static_cast<int>(savedQuality)));

	m_pQualityHint = new QLabel(this);
	m_pQualityHint->setEnabled(false);

	connect(m_pQuality, &QComboBox::currentIndexChanged, this, &CMainWindow::UpdateQualityHint);
	connect(m_pCodec, &QComboBox::currentIndexChanged, this, &CMainWindow::UpdateQualityHint);
	connect(m_pFrameRate, &QComboBox::currentIndexChanged, this, &CMainWindow::UpdateQualityHint);
	connect(m_pSource, &QComboBox::currentIndexChanged, this, &CMainWindow::UpdateQualityHint);
	connect(m_pSource, &QComboBox::currentIndexChanged, this, &CMainWindow::RefreshSourceOptions);
	UpdateQualityHint();

	QVBoxLayout* const pQualityColumn{ new QVBoxLayout };
	pQualityColumn->setContentsMargins(0, 0, 0, 0);
	pQualityColumn->setSpacing(2);
	pQualityColumn->addWidget(m_pQuality);
	pQualityColumn->addWidget(m_pQualityHint);

	int const savedSource{ settings.value(SourceKey, SourceScreen).toInt() };
	int const sourceIndex{ m_pSource->findData(savedSource) };
	m_pSource->setCurrentIndex(sourceIndex >= 0 ? sourceIndex : 0);
	m_pRememberWindow->setChecked(settings.value(RememberWindowKey, false).toBool());
	RefreshSourceOptions();

	QFormLayout* const pForm{ new QFormLayout };
	pForm->addRow(tr("Record"), m_pSource);
	pForm->addRow(QString{}, m_pRememberWindow);
	pForm->addRow(tr("Save to"), pDirectoryRow);
	pForm->addRow(tr("Format"), m_pContainer);
	pForm->addRow(tr("Codec"), m_pCodec);
	pForm->addRow(tr("Frame rate"), m_pFrameRate);
	pForm->addRow(tr("Quality"), pQualityColumn);

	m_pRecord = new QPushButton(tr("Record"), this);
	m_pRecord->setMinimumHeight(44);
	m_pRecord->setStyleSheet(QString::fromUtf8(RecordButtonStyle));
	connect(m_pRecord, &QPushButton::clicked, this, &CMainWindow::OnRecordPressed);

	m_pElapsed = new QLabel(FormatDuration(0), this);
	m_pStatus = new QLabel(tr("Ready"), this);
	m_pStatus->setAlignment(Qt::AlignRight | Qt::AlignVCenter);

	QHBoxLayout* const pStatusRow{ new QHBoxLayout };
	pStatusRow->addWidget(m_pElapsed);
	pStatusRow->addWidget(m_pStatus, 1);

	BuildAudioGroup();
	ConnectSettingSaves();

	QVBoxLayout* const pLayout{ new QVBoxLayout(this) };
	pLayout->addLayout(pForm);
	pLayout->addWidget(m_pAudioGroup);
	pLayout->addSpacing(8);
	pLayout->addWidget(m_pRecord);
	pLayout->addLayout(pStatusRow);
}

//////////////////////////////////////////////////////////////////////////
// Connected after every control has been restored, so restoring does not write back what it just read.
void CMainWindow::ConnectSettingSaves()
{
	for (QComboBox* const pBox : { m_pSource, m_pContainer, m_pCodec, m_pFrameRate, m_pQuality,
	                               m_pSystemDevice, m_pMicrophoneDevice, m_pAudioQuality })
	{
		connect(pBox, &QComboBox::currentIndexChanged, this, &CMainWindow::SaveSettings);
	}

	for (QCheckBox* const pBox : { m_pRememberWindow, m_pSystemEnabled, m_pMicrophoneEnabled })
	{
		connect(pBox, &QCheckBox::toggled, this, &CMainWindow::SaveSettings);
	}

	for (QSlider* const pGain : { m_pSystemGain, m_pMicrophoneGain })
	{
		connect(pGain, &QSlider::valueChanged, this, &CMainWindow::SaveSettings);
	}
}

//////////////////////////////////////////////////////////////////////////
void CMainWindow::SaveSettings() const
{
	QSettings settings;

	settings.setValue(DirectoryKey, m_pDirectory->text());
	settings.setValue(SourceKey, m_pSource->currentData().toInt());
	settings.setValue(RememberWindowKey, m_pRememberWindow->isChecked());
	settings.setValue(ContainerKey, ToQString(Encode::GetContainerName(CurrentContainer())));
	settings.setValue(CodecKey, ToQString(Encode::GetCodecName(CurrentCodec())));
	settings.setValue(QualityKey, ToQString(Encode::GetQualityName(CurrentQuality())));
	settings.setValue(FrameRateKey, static_cast<int>(CurrentMaxFrameRate()));
	settings.setValue(SystemAudioKey, m_pSystemEnabled->isChecked());
	settings.setValue(SystemDeviceKey, m_pSystemDevice->currentData().toString());
	settings.setValue(MicrophoneKey, m_pMicrophoneEnabled->isChecked());
	settings.setValue(MicrophoneDeviceKey, m_pMicrophoneDevice->currentData().toString());
	settings.setValue(AudioQualityKey, ToQString(Encode::GetQualityName(CurrentAudioQuality())));
	settings.setValue(SystemGainKey, m_pSystemGain->value());
	settings.setValue(MicrophoneGainKey, m_pMicrophoneGain->value());
}

//////////////////////////////////////////////////////////////////////////
void CMainWindow::BuildAudioGroup()
{
	QSettings settings;

	m_pAudioGroup = new QGroupBox(tr("Audio"), this);

	m_pSystemEnabled = new QCheckBox(tr("System"), m_pAudioGroup);
	m_pSystemEnabled->setChecked(settings.value(SystemAudioKey, false).toBool());
	m_pSystemEnabled->setToolTip(tr("Record what the machine plays, whatever the speakers are set to."));

	m_pMicrophoneEnabled = new QCheckBox(tr("Microphone"), m_pAudioGroup);
	m_pMicrophoneEnabled->setChecked(settings.value(MicrophoneKey, false).toBool());

	m_pSystemDevice = new QComboBox(m_pAudioGroup);
	m_pMicrophoneDevice = new QComboBox(m_pAudioGroup);

	// A combo asks for its longest entry, and device names run well past the window's fixed width.
	for (QComboBox* const pDevice : { m_pSystemDevice, m_pMicrophoneDevice })
	{
		pDevice->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
		pDevice->setMinimumContentsLength(16);
	}

	RefreshAudioDevices();

	m_pSystemGain = new QSlider(Qt::Horizontal, m_pAudioGroup);
	m_pMicrophoneGain = new QSlider(Qt::Horizontal, m_pAudioGroup);
	m_pSystemGainValue = new QLabel(m_pAudioGroup);
	m_pMicrophoneGainValue = new QLabel(m_pAudioGroup);

	for (QSlider* const pGain : { m_pSystemGain, m_pMicrophoneGain })
	{
		pGain->setRange(MinimumGainDecibels, MaximumGainDecibels);
		pGain->setTickInterval(10);
		pGain->setToolTip(tr("Klip's own level for this source. Your system volumes are left alone."));
		connect(pGain, &QSlider::valueChanged, this, &CMainWindow::OnGainChanged);
	}

	m_pSystemGain->setValue(settings.value(SystemGainKey, 0).toInt());
	m_pMicrophoneGain->setValue(settings.value(MicrophoneGainKey, 0).toInt());

	for (QLabel* const pValue : { m_pSystemGainValue, m_pMicrophoneGainValue })
	{
		pValue->setEnabled(false);
		pValue->setMinimumWidth(GainValueWidth);
		pValue->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
	}

	m_pAudioQuality = new QComboBox(m_pAudioGroup);

	for (size_t index{ 0 }; index < Encode::QualityCount; ++index)
	{
		Encode::EQuality const quality{ static_cast<Encode::EQuality>(index) };
		m_pAudioQuality->addItem(QualityLabel(quality), static_cast<int>(quality));
	}

	Encode::EQuality const savedQuality{ Encode::ParseQuality(
		settings.value(AudioQualityKey, QStringLiteral("high")).toString().toStdString(),
		Encode::EQuality::High) };

	m_pAudioQuality->setCurrentIndex(m_pAudioQuality->findData(static_cast<int>(savedQuality)));

	m_pAudioQualityHint = new QLabel(m_pAudioGroup);
	m_pAudioQualityHint->setEnabled(false);

	m_pSystemMeter = new CLevelMeter(m_pAudioGroup);
	m_pMicrophoneMeter = new CLevelMeter(m_pAudioGroup);

	connect(m_pSystemEnabled, &QCheckBox::toggled, this, &CMainWindow::OnAudioSourceToggled);
	connect(m_pMicrophoneEnabled, &QCheckBox::toggled, this, &CMainWindow::OnAudioSourceToggled);
	connect(m_pAudioQuality, &QComboBox::currentIndexChanged, this,
	        &CMainWindow::UpdateAudioQualityHint);
	connect(m_pAudioQuality, &QComboBox::currentIndexChanged, this, &CMainWindow::UpdateQualityHint);
	connect(m_pContainer, &QComboBox::currentIndexChanged, this,
	        &CMainWindow::RefreshAudioAvailability);
	connect(m_pSystemDevice, &QComboBox::currentIndexChanged, this, &CMainWindow::RefreshMonitoring);
	connect(m_pMicrophoneDevice, &QComboBox::currentIndexChanged, this,
	        &CMainWindow::RefreshMonitoring);

	QVBoxLayout* const pQualityColumn{ new QVBoxLayout };
	pQualityColumn->setContentsMargins(0, 0, 0, 0);
	pQualityColumn->setSpacing(2);
	pQualityColumn->addWidget(m_pAudioQuality);
	pQualityColumn->addWidget(m_pAudioQualityHint);

	QFormLayout* const pAudioForm{ new QFormLayout(m_pAudioGroup) };
	QHBoxLayout* const pSystemGainRow{ new QHBoxLayout };
	pSystemGainRow->setContentsMargins(0, 0, 0, 0);
	pSystemGainRow->addWidget(m_pSystemGain, 1);
	pSystemGainRow->addWidget(m_pSystemGainValue);

	QHBoxLayout* const pMicrophoneGainRow{ new QHBoxLayout };
	pMicrophoneGainRow->setContentsMargins(0, 0, 0, 0);
	pMicrophoneGainRow->addWidget(m_pMicrophoneGain, 1);
	pMicrophoneGainRow->addWidget(m_pMicrophoneGainValue);

	pAudioForm->addRow(m_pSystemEnabled, m_pSystemDevice);
	pAudioForm->addRow(QString{}, pSystemGainRow);
	pAudioForm->addRow(QString{}, m_pSystemMeter);
	pAudioForm->addRow(m_pMicrophoneEnabled, m_pMicrophoneDevice);
	pAudioForm->addRow(QString{}, pMicrophoneGainRow);
	pAudioForm->addRow(QString{}, m_pMicrophoneMeter);
	pAudioForm->addRow(tr("Quality"), pQualityColumn);

	OnGainChanged();
	OnAudioSourceToggled();
	UpdateAudioQualityHint();
	RefreshAudioAvailability();
}

//////////////////////////////////////////////////////////////////////////
bool CMainWindow::Initialize()
{
	if (!m_audioDevices.Initialize())
	{
		gLog.Warning("No audio devices could be listed; Klip will record silent.");
	}

	// Before the layout: building the audio group already asks the monitors to follow the ticked set.
	m_pMeterTimer = new QTimer(this);
	m_pMeterTimer->setInterval(MeterMilliseconds);
	connect(m_pMeterTimer, &QTimer::timeout, this, &CMainWindow::OnMeterTick);

	BuildLayout();

	m_pTimer = new QTimer(this);
	m_pTimer->setInterval(TickMilliseconds);
	connect(m_pTimer, &QTimer::timeout, this, &CMainWindow::OnTick);

	m_pTray = new CTrayIcon(this);
	m_pTray->Initialize();

	connect(m_pTray, &CTrayIcon::ToggleRequested, this, &CMainWindow::OnRecordPressed);
	connect(m_pTray, &CTrayIcon::ShowRequested, this, &CMainWindow::Reveal);
	connect(m_pTray, &CTrayIcon::QuitRequested, qApp, &QApplication::quit);

	m_session.SetEndedCallback([this]() {
		// Arrives on the PipeWire thread; the widgets are the UI thread's.
		QMetaObject::invokeMethod(this, [this]() { OnCaptureWithdrawn(); }, Qt::QueuedConnection);
	});

	return m_session.Initialize();
}

//////////////////////////////////////////////////////////////////////////
void CMainWindow::Terminate()
{
	if (m_session.IsRecording())
	{
		StopRecording();
	}

	StopMonitoring();

	if (m_pTray != nullptr)
	{
		m_pTray->Terminate();
	}

	m_session.Terminate();
	m_audioDevices.Terminate();
}

//////////////////////////////////////////////////////////////////////////
QString CMainWindow::OutputDirectory() const
{
	QSettings settings;

	QString const fallback{ QStandardPaths::writableLocation(QStandardPaths::MoviesLocation) +
		                    QStringLiteral("/klip-captures") };

	return settings.value(DirectoryKey, fallback).toString();
}

//////////////////////////////////////////////////////////////////////////
QString CMainWindow::ContainerLabel(Encode::EContainer container) const
{
	QString label;

	switch (container)
	{
		case Encode::EContainer::Mp4:      label = tr("MP4"); break;
		case Encode::EContainer::Matroska: label = tr("Matroska (MKV)"); break;
		case Encode::EContainer::WebM:     label = tr("WebM"); break;
		case Encode::EContainer::Count:    break;
	}

	return label;
}

//////////////////////////////////////////////////////////////////////////
QString CMainWindow::CodecLabel(Encode::ECodec codec) const
{
	QString label;

	switch (codec)
	{
		case Encode::ECodec::H264:  label = tr("H.264"); break;
		case Encode::ECodec::Hevc:  label = tr("HEVC (H.265)"); break;
		case Encode::ECodec::Av1:   label = tr("AV1"); break;
		case Encode::ECodec::Count: break;
	}

	return label;
}

//////////////////////////////////////////////////////////////////////////
QString CMainWindow::QualityLabel(Encode::EQuality quality) const
{
	QString label;

	switch (quality)
	{
		case Encode::EQuality::Smallest: label = tr("Smallest file"); break;
		case Encode::EQuality::Smaller:  label = tr("Smaller file"); break;
		case Encode::EQuality::Balanced: label = tr("Balanced"); break;
		case Encode::EQuality::High:     label = tr("High"); break;
		case Encode::EQuality::Best:     label = tr("Best quality"); break;
		case Encode::EQuality::Count:    break;
	}

	return label;
}

//////////////////////////////////////////////////////////////////////////
Encode::EContainer CMainWindow::CurrentContainer() const
{
	return static_cast<Encode::EContainer>(m_pContainer->currentData().toInt());
}

//////////////////////////////////////////////////////////////////////////
Encode::ECodec CMainWindow::CurrentCodec() const
{
	return static_cast<Encode::ECodec>(m_pCodec->currentData().toInt());
}

//////////////////////////////////////////////////////////////////////////
Encode::EQuality CMainWindow::CurrentQuality() const
{
	return static_cast<Encode::EQuality>(m_pQuality->currentData().toInt());
}

//////////////////////////////////////////////////////////////////////////
uint32_t CMainWindow::CurrentMaxFrameRate() const
{
	return static_cast<uint32_t>(m_pFrameRate->currentData().toInt());
}

//////////////////////////////////////////////////////////////////////////
void CMainWindow::RefreshAudioDevices()
{
	m_audioDevices.Refresh();

	QString const wantedSink{ m_pSystemDevice->count() > 0
		                          ? m_pSystemDevice->currentData().toString()
		                          : QSettings{}.value(SystemDeviceKey).toString() };
	QString const wantedSource{ m_pMicrophoneDevice->count() > 0
		                            ? m_pMicrophoneDevice->currentData().toString()
		                            : QSettings{}.value(MicrophoneDeviceKey).toString() };

	QSignalBlocker const systemBlocker{ m_pSystemDevice };
	QSignalBlocker const microphoneBlocker{ m_pMicrophoneDevice };

	m_pSystemDevice->clear();
	m_pMicrophoneDevice->clear();

	for (Capture::SAudioDevice const& device : m_audioDevices.GetSinks())
	{
		m_pSystemDevice->addItem(QString::fromStdString(device.description),
		                         QString::fromStdString(device.nodeName));
	}

	for (Capture::SAudioDevice const& device : m_audioDevices.GetSources())
	{
		m_pMicrophoneDevice->addItem(QString::fromStdString(device.description),
		                             QString::fromStdString(device.nodeName));
	}

	int const sinkIndex{ m_pSystemDevice->findData(wantedSink) };
	m_pSystemDevice->setCurrentIndex(sinkIndex >= 0 ? sinkIndex : 0);

	int const sourceIndex{ m_pMicrophoneDevice->findData(wantedSource) };
	m_pMicrophoneDevice->setCurrentIndex(sourceIndex >= 0 ? sourceIndex : 0);
}

//////////////////////////////////////////////////////////////////////////
void CMainWindow::RefreshAudioAvailability()
{
	bool const carries{ Encode::ContainerCarriesAudio(CurrentContainer()) };

	SetAudioInputsEnabled(carries);
	RefreshMeterVisibility();

	m_pAudioGroup->setToolTip(carries ? QString{}
	                                  : tr("WebM carries only Opus or Vorbis, which this build cannot "
	                                       "write. Choose MP4 or Matroska to record sound."));

	RefreshMonitoring();
	UpdateQualityHint();
}

//////////////////////////////////////////////////////////////////////////
void CMainWindow::UpdateAudioQualityHint()
{
	int const bitsPerSecond{ Encode::GetAudioBitsPerSecond(CurrentAudioQuality()) };

	m_pAudioQualityHint->setText(tr("AAC, %1 kbps stereo").arg(bitsPerSecond / 1000));
}

//////////////////////////////////////////////////////////////////////////
void CMainWindow::OnAudioSourceToggled()
{
	m_pSystemDevice->setEnabled(m_pSystemEnabled->isChecked());
	m_pMicrophoneDevice->setEnabled(m_pMicrophoneEnabled->isChecked());
	m_pSystemGain->setEnabled(m_pSystemEnabled->isChecked());
	m_pMicrophoneGain->setEnabled(m_pMicrophoneEnabled->isChecked());

	RefreshMeterVisibility();
	RefreshMonitoring();
	UpdateQualityHint();
}

//////////////////////////////////////////////////////////////////////////
void CMainWindow::RefreshMonitoring()
{
	bool const canMonitor{ isVisible() && m_pAudioGroup->isEnabled() && !m_session.IsRecording() };

	QString const wantedSystem{ canMonitor && WantsSystemAudio()
		                           ? m_pSystemDevice->currentData().toString()
		                           : QString{} };
	QString const wantedMicrophone{ canMonitor && WantsMicrophone()
		                               ? m_pMicrophoneDevice->currentData().toString()
		                               : QString{} };

	if (wantedSystem != m_systemMonitored)
	{
		m_systemMonitor.Terminate();
		m_systemMonitored.clear();
		m_numSystemBuffers = 0;
		m_pSystemMeter->Reset();

		if (!wantedSystem.isEmpty())
		{
			m_systemMonitored = m_systemMonitor.Initialize(CurrentSystemDevice(), {}, {})
			                        ? wantedSystem
			                        : QString{};
		}

		m_pSystemMeter->SetUnavailable(!wantedSystem.isEmpty() && m_systemMonitored.isEmpty());
	}

	if (wantedMicrophone != m_microphoneMonitored)
	{
		m_microphoneMonitor.Terminate();
		m_microphoneMonitored.clear();
		m_numMicrophoneBuffers = 0;
		m_pMicrophoneMeter->Reset();

		if (!wantedMicrophone.isEmpty())
		{
			m_microphoneMonitored = m_microphoneMonitor.Initialize(CurrentMicrophoneDevice(), {}, {})
			                            ? wantedMicrophone
			                            : QString{};
		}

		m_pMicrophoneMeter->SetUnavailable(!wantedMicrophone.isEmpty() &&
		                                   m_microphoneMonitored.isEmpty());
	}

	if (m_systemMonitored.isEmpty() && m_microphoneMonitored.isEmpty() && !m_session.IsRecording())
	{
		m_pMeterTimer->stop();
	}
	else
	{
		m_pMeterTimer->start();
	}
}

//////////////////////////////////////////////////////////////////////////
void CMainWindow::UpdateMeter(CLevelMeter& meter, Capture::CAudioStream const& stream,
                              uint64_t& numBuffers, float gain)
{
	uint64_t const current{ stream.GetNumBuffers() };

	float peaks[Capture::MaxAudioChannels]{ 0.0f, 0.0f };

	for (uint32_t channel{ 0 }; channel < Capture::MaxAudioChannels; ++channel)
	{
		// No new buffer means no new sound, not the last one held forever.
		peaks[channel] = current != numBuffers ? stream.GetChannelPeak(channel) * gain : 0.0f;
	}

	numBuffers = current;
	meter.SetPeaks(peaks, Capture::MaxAudioChannels);
}

//////////////////////////////////////////////////////////////////////////
void CMainWindow::OnMeterTick()
{
	// While recording the session owns the devices, so the meters read what is being written rather
	// than a preview that is no longer open.
	bool const recording{ m_session.IsRecording() };

	if (recording || !m_systemMonitored.isEmpty())
	{
		UpdateMeter(*m_pSystemMeter, recording ? m_session.GetSystemAudio() : m_systemMonitor,
		            m_numSystemBuffers, SystemGain());
	}

	if (recording || !m_microphoneMonitored.isEmpty())
	{
		UpdateMeter(*m_pMicrophoneMeter,
		            recording ? m_session.GetMicrophoneAudio() : m_microphoneMonitor,
		            m_numMicrophoneBuffers, MicrophoneGain());
	}
}

//////////////////////////////////////////////////////////////////////////
void CMainWindow::StopMonitoring()
{
	m_pMeterTimer->stop();

	m_systemMonitor.Terminate();
	m_microphoneMonitor.Terminate();

	m_systemMonitored.clear();
	m_microphoneMonitored.clear();

	m_numSystemBuffers = 0;
	m_numMicrophoneBuffers = 0;

	m_pSystemMeter->Reset();
	m_pMicrophoneMeter->Reset();

	m_pSystemMeter->SetUnavailable(false);
	m_pMicrophoneMeter->SetUnavailable(false);
}

//////////////////////////////////////////////////////////////////////////
bool CMainWindow::WantsSystemAudio() const
{
	return m_pSystemEnabled->isChecked() && m_pSystemDevice->count() > 0;
}

//////////////////////////////////////////////////////////////////////////
bool CMainWindow::WantsMicrophone() const
{
	return m_pMicrophoneEnabled->isChecked() && m_pMicrophoneDevice->count() > 0;
}

//////////////////////////////////////////////////////////////////////////
Capture::SAudioDevice CMainWindow::CurrentSystemDevice() const
{
	Capture::SAudioDevice device;
	QString const wanted{ m_pSystemDevice->currentData().toString() };

	for (Capture::SAudioDevice const& candidate : m_audioDevices.GetSinks())
	{
		if (QString::fromStdString(candidate.nodeName) == wanted)
		{
			device = candidate;
		}
	}

	return device;
}

//////////////////////////////////////////////////////////////////////////
Capture::SAudioDevice CMainWindow::CurrentMicrophoneDevice() const
{
	Capture::SAudioDevice device;
	QString const wanted{ m_pMicrophoneDevice->currentData().toString() };

	for (Capture::SAudioDevice const& candidate : m_audioDevices.GetSources())
	{
		if (QString::fromStdString(candidate.nodeName) == wanted)
		{
			device = candidate;
		}
	}

	return device;
}

//////////////////////////////////////////////////////////////////////////
float CMainWindow::SystemGain() const
{
	return FromDecibels(m_pSystemGain->value());
}

//////////////////////////////////////////////////////////////////////////
float CMainWindow::MicrophoneGain() const
{
	return FromDecibels(m_pMicrophoneGain->value());
}

//////////////////////////////////////////////////////////////////////////
void CMainWindow::OnGainChanged()
{
	m_pSystemGainValue->setText(tr("%1 dB").arg(m_pSystemGain->value()));
	m_pMicrophoneGainValue->setText(tr("%1 dB").arg(m_pMicrophoneGain->value()));
}

//////////////////////////////////////////////////////////////////////////
Encode::EQuality CMainWindow::CurrentAudioQuality() const
{
	return static_cast<Encode::EQuality>(m_pAudioQuality->currentData().toInt());
}

//////////////////////////////////////////////////////////////////////////
void CMainWindow::RefreshSourceOptions()
{
	m_pRememberWindow->setEnabled(m_pSource->currentData().toInt() == SourceWindow);
}

//////////////////////////////////////////////////////////////////////////
void CMainWindow::RefreshCodecs()
{
	Encode::EContainer const container{ CurrentContainer() };
	Encode::ECodec const     wanted{ m_pCodec->count() > 0 ? CurrentCodec() : Encode::ECodec::H264 };

	m_pCodec->clear();

	for (size_t index{ 0 }; index < Encode::CodecCount; ++index)
	{
		Encode::ECodec const codec{ static_cast<Encode::ECodec>(index) };

		if (Encode::ContainerAccepts(container, codec) && Encode::IsCodecOffered(codec))
		{
			m_pCodec->addItem(CodecLabel(codec), static_cast<int>(codec));
		}
	}

	int const index{ m_pCodec->findData(static_cast<int>(wanted)) };
	m_pCodec->setCurrentIndex(index >= 0 ? index : 0);
}

//////////////////////////////////////////////////////////////////////////
void CMainWindow::UpdateQualityHint()
{
	QScreen const* const pScreen{ QGuiApplication::primaryScreen() };
	QSize const          screen{ pScreen->geometry().size() };

	// Uncapped settles at the panel's refresh rate, which is not always 60.
	uint32_t const refresh{ static_cast<uint32_t>(std::lround(pScreen->refreshRate())) };
	uint32_t const cap{ CurrentMaxFrameRate() };
	uint32_t const rate{ cap != 0 ? cap : (refresh != 0 ? refresh : FallbackFrameRate) };

	// Only a whole screen has a size before the portal answers; a window or a region is whatever the
	// user is about to point at.
	bool const knowsSize{ m_pSource->currentData().toInt() == SourceScreen };

	uint64_t bitsPerSecond{ Encode::EstimateBitsPerSecond(
		CurrentCodec(), CurrentQuality(), static_cast<uint32_t>(screen.width()),
		static_cast<uint32_t>(screen.height()), rate) };

	if (m_pAudioGroup != nullptr && m_pAudioGroup->isEnabled() &&
	    (WantsSystemAudio() || WantsMicrophone()))
	{
		bitsPerSecond += static_cast<uint64_t>(Encode::GetAudioBitsPerSecond(CurrentAudioQuality()));
	}

	double const mebibytesPerMinute{ static_cast<double>(bitsPerSecond) * 60.0 / 8.0 /
		                             (1024.0 * 1024.0) };

	// Measured at 8.5x the table on a game, so this is nothing like a ceiling and must not read as one.
	m_pQualityHint->setText(
		knowsSize ? tr("around %1 MiB per minute at %2x%3, more for video or games")
		                .arg(mebibytesPerMinute, 0, 'f', 1)
		                .arg(screen.width())
		                .arg(screen.height())
		          : tr("around %1 MiB per minute for a whole screen, less for a smaller area")
		                .arg(mebibytesPerMinute, 0, 'f', 1));
}

//////////////////////////////////////////////////////////////////////////
QString CMainWindow::MakeOutputPath() const
{
	QString const stamp{ QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss")) };
	QString const extension{ ToQString(Encode::GetContainerExtension(CurrentContainer())) };

	return QDir{ m_pDirectory->text() }.filePath(QStringLiteral("klip-%1.%2").arg(stamp, extension));
}

//////////////////////////////////////////////////////////////////////////
void CMainWindow::OnBrowsePressed()
{
	QString const chosen{ QFileDialog::getExistingDirectory(this, tr("Save recordings to"),
	                                                        m_pDirectory->text()) };

	if (!chosen.isEmpty())
	{
		m_pDirectory->setText(chosen);

		SaveSettings();
	}
}

//////////////////////////////////////////////////////////////////////////
void CMainWindow::ShowInFileManager(QString const& filePath)
{
	bool shown{ false };

	if (!filePath.isEmpty())
	{
		// Selects the file rather than just opening the folder, where the desktop implements it.
		QDBusInterface manager{ QStringLiteral("org.freedesktop.FileManager1"),
			                    QStringLiteral("/org/freedesktop/FileManager1"),
			                    QStringLiteral("org.freedesktop.FileManager1"),
			                    QDBusConnection::sessionBus() };

		if (manager.isValid())
		{
			QStringList const uris{ QUrl::fromLocalFile(filePath).toString() };
			QDBusMessage const reply{ manager.call(QStringLiteral("ShowItems"), uris, QString{}) };

			shown = reply.type() != QDBusMessage::ErrorMessage;
		}
	}

	if (!shown && !QDesktopServices::openUrl(QUrl::fromLocalFile(m_pDirectory->text())))
	{
		gLog.Error("Could not open {}", m_pDirectory->text().toStdString());
	}
}

//////////////////////////////////////////////////////////////////////////
void CMainWindow::OnOpenPressed()
{
	ShowInFileManager(m_session.IsRecording() ? QString{} : m_currentPath);
}

//////////////////////////////////////////////////////////////////////////
void CMainWindow::OnRecordPressed()
{
	if (m_session.IsRecording())
	{
		StopRecording();
	}
	else
	{
		StartRecording();
	}
}

//////////////////////////////////////////////////////////////////////////
bool CMainWindow::ChooseRegion()
{
	hide();
	SettleAfterHiding();

	CRegionSelector selector;
	QRect const chosen{ selector.Choose() };

	m_region = Encode::SRegion{ static_cast<uint32_t>(chosen.x()), static_cast<uint32_t>(chosen.y()),
		                        static_cast<uint32_t>(chosen.width()),
		                        static_cast<uint32_t>(chosen.height()) };

	bool const chose{ !chosen.isEmpty() };

	if (chose)
	{
		SettleAfterHiding();
	}

	return chose;
}

//////////////////////////////////////////////////////////////////////////
void CMainWindow::StartRecording()
{
	QDir const directory{ m_pDirectory->text() };

	int const source{ m_pSource->currentData().toInt() };

	m_region = Encode::SRegion{};

	if (!directory.exists() && !directory.mkpath(QStringLiteral(".")))
	{
		gLog.Error("Cannot create or write to {}", m_pDirectory->text().toStdString());
		ShowIdleState(tr("Cannot write to that folder"));
	}
	else if (source == SourceRegion && !ChooseRegion())
	{
		show();
		ShowIdleState(tr("Ready"));
	}
	else
	{
		m_currentPath = MakeOutputPath();

		StopMonitoring();

		m_pRecord->setEnabled(false);
		m_pStatus->setText(tr("Waiting for permission…"));

		SetInputsEnabled(false);

		// Out of shot before the stream opens; hiding once it runs films the window fading out.
		if (isVisible() && m_pTray->IsAvailable())
		{
			hide();
			SettleAfterHiding();
		}

		Klip::SRecordingRequest request;
		request.outputPath = m_currentPath.toStdString();
		request.codec = CurrentCodec();
		request.quality = CurrentQuality();
		request.maxFrameRate = CurrentMaxFrameRate();
		request.audioQuality = CurrentAudioQuality();
		request.systemGain = SystemGain();
		request.microphoneGain = MicrophoneGain();

		if (Encode::ContainerCarriesAudio(CurrentContainer()))
		{
			if (WantsSystemAudio())
			{
				request.systemAudio = CurrentSystemDevice();
			}

			if (WantsMicrophone())
			{
				request.microphone = CurrentMicrophoneDevice();
			}
		}
		request.region = m_region;
		request.rememberWindow = m_pRememberWindow->isChecked();
		request.source = source == SourceWindow ? Klip::Capture::ESourceType::Window
		                                        : Klip::Capture::ESourceType::Screen;

		m_session.Start(request, [this](bool started) {
			m_pRecord->setEnabled(true);

			if (started)
			{
				m_clock.start();
				m_pTimer->start();
				m_pRecord->setText(tr("Stop"));
				m_pStatus->setText(tr("Recording"));
				m_pTray->SetRecording(true);

				RefreshMonitoring();
			}
			else
			{
				gLog.Warning("The recording did not start.");
				show();

				std::string const& failure{ m_session.GetStartFailure() };

				ShowIdleState(failure.empty()
				                  ? tr("Recording was not permitted")
				                  : tr("%1 is unavailable — pick another device")
				                        .arg(QString::fromStdString(failure)));
			}
		});
	}
}

//////////////////////////////////////////////////////////////////////////
void CMainWindow::StopRecording()
{
	m_pTimer->stop();

	bool const written{ m_session.Stop() };
	SSessionStats const stats{ m_session.GetStats() };

	if (written)
	{
		gLog.Info("Captured {}, encoded {}, dropped {}", stats.numCaptured, stats.numEncoded,
		          stats.numDropped);
		ShowIdleState(tr("Saved %1").arg(QFileInfo{ m_currentPath }.fileName()));
	}
	else
	{
		gLog.Error("The recording could not be finalised; {} may be unusable.",
		           m_currentPath.toStdString());
		ShowIdleState(tr("Recording failed"));
	}

	m_pElapsed->setText(FormatDuration(0));
	show();
	raise();

	// show() fires no event when the window never hid, and the previews are still closed.
	RefreshMonitoring();
}

//////////////////////////////////////////////////////////////////////////
void CMainWindow::SetInputsEnabled(bool enabled)
{
	m_pSource->setEnabled(enabled);
	m_pDirectory->setEnabled(enabled);
	m_pBrowse->setEnabled(enabled);
	m_pContainer->setEnabled(enabled);
	m_pCodec->setEnabled(enabled);
	m_pFrameRate->setEnabled(enabled);
	m_pQuality->setEnabled(enabled);

	SetAudioInputsEnabled(enabled && Encode::ContainerCarriesAudio(CurrentContainer()));
}

//////////////////////////////////////////////////////////////////////////
void CMainWindow::SetAudioInputsEnabled(bool enabled)
{
	m_pSystemEnabled->setEnabled(enabled);
	m_pMicrophoneEnabled->setEnabled(enabled);
	m_pSystemDevice->setEnabled(enabled && m_pSystemEnabled->isChecked());
	m_pMicrophoneDevice->setEnabled(enabled && m_pMicrophoneEnabled->isChecked());
	m_pAudioQuality->setEnabled(enabled);
	m_pSystemGain->setEnabled(enabled && m_pSystemEnabled->isChecked());
	m_pMicrophoneGain->setEnabled(enabled && m_pMicrophoneEnabled->isChecked());
}

//////////////////////////////////////////////////////////////////////////
void CMainWindow::RefreshMeterVisibility()
{
	bool const carries{ Encode::ContainerCarriesAudio(CurrentContainer()) };
	bool const system{ carries && m_pSystemEnabled->isChecked() };
	bool const microphone{ carries && m_pMicrophoneEnabled->isChecked() };

	m_pSystemMeter->setVisible(system);
	m_pSystemGain->setVisible(system);
	m_pSystemGainValue->setVisible(system);

	m_pMicrophoneMeter->setVisible(microphone);
	m_pMicrophoneGain->setVisible(microphone);
	m_pMicrophoneGainValue->setVisible(microphone);

	if (layout() != nullptr)
	{
		m_pAudioGroup->layout()->activate();
		setFixedHeight(sizeHint().height());
	}
}

//////////////////////////////////////////////////////////////////////////
void CMainWindow::ShowIdleState(QString const& message)
{
	m_pRecord->setText(tr("Record"));
	m_pRecord->setEnabled(true);
	m_pStatus->setText(message);
	SetInputsEnabled(true);

	if (m_pTray != nullptr)
	{
		m_pTray->SetRecording(false);
	}
}

//////////////////////////////////////////////////////////////////////////
void CMainWindow::Reveal()
{
	showNormal();
	raise();
	activateWindow();
}

//////////////////////////////////////////////////////////////////////////
void CMainWindow::OnCaptureWithdrawn()
{
	if (m_session.IsRecording())
	{
		StopRecording();
		m_pStatus->setText(tr("Screen sharing was stopped"));
	}
}

//////////////////////////////////////////////////////////////////////////
void CMainWindow::OnTick()
{
	SSessionStats const stats{ m_session.GetStats() };

	qint64 const      elapsedMs{ m_clock.elapsed() };
	QString const     elapsed{ FormatDuration(elapsedMs) };
	QString const     written{ FormatBytes(stats.bytesWritten) };
	SThroughput const rate{ FormatThroughput(stats.bytesWritten, elapsedMs / 1000) };

	QString const detail{ rate.perMinute.isEmpty()
		                      ? written
		                      : QStringLiteral("%1 \u00b7 %2 \u00b7 %3")
		                            .arg(written, rate.perMinute, rate.perHour) };

	// GNOME's indicator extension renders the label but declines to render a tooltip, so this is the
	// only figure visible while recording -- and the window is hidden then, so it is the hourly one.
	QString const label{ rate.perHour.isEmpty()
		                     ? elapsed
		                     : QStringLiteral("%1 \u00b7 %2").arg(elapsed, rate.perHour) };

	m_pElapsed->setText(elapsed);
	m_pStatus->setText(detail);
	m_pTray->SetLabel(label);
	m_pTray->SetDetail(QStringLiteral("%1 \u00b7 %2").arg(elapsed, detail));
}

//////////////////////////////////////////////////////////////////////////
void CMainWindow::closeEvent(QCloseEvent* pEvent)
{
	if (m_pTray != nullptr && m_pTray->IsAvailable())
	{
		hide();
		pEvent->ignore();
	}
	else
	{
		pEvent->accept();
		qApp->quit();
	}
}

//////////////////////////////////////////////////////////////////////////
void CMainWindow::showEvent(QShowEvent* pEvent)
{
	QWidget::showEvent(pEvent);

	RefreshAudioDevices();
	RefreshMeterVisibility();
	RefreshMonitoring();
}

//////////////////////////////////////////////////////////////////////////
void CMainWindow::hideEvent(QHideEvent* pEvent)
{
	QWidget::hideEvent(pEvent);

	RefreshMonitoring();
}
} // namespace Klip
