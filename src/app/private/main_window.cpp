#include "main_window.hpp"

#include "bus/connection.hpp"
#include "level_meter.hpp"
#include "log.hpp"
#include "recorder/frame_rates.hpp"
#include "recorder/labels.hpp"
#include "region_selector.hpp"
#include "settings_store.hpp"
#include "tray_image.hpp"

#include <QtCore/QEventLoop>
#include <QtCore/QMetaObject>
#include <QtCore/QSignalBlocker>
#include <QtCore/QTimer>
#include <QtCore/QUrl>
#include <QtGui/QCloseEvent>
#include <QtGui/QDesktopServices>
#include <QtGui/QGuiApplication>
#include <QtGui/QHideEvent>
#include <QtGui/QScreen>
#include <QtGui/QShowEvent>
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
#include <systemd/sd-bus.h>
#include <tge/profiling/profiling.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <initializer_list>
#include <span>
#include <string_view>

namespace Klip
{
namespace
{
constexpr int GainValueWidth{ 52 };
constexpr int WindowWidth{ 600 };

constexpr char const* RecordButtonStyle{
	"QPushButton { border: 1px solid rgba(235, 110, 40, 0.55); border-radius: 4px; }"
	"QPushButton:hover { border-color: rgba(235, 110, 40, 0.9);"
	"                    background-color: rgba(235, 110, 40, 0.10); }"
	"QPushButton:pressed { background-color: rgba(235, 110, 40, 0.18); }"
	"QPushButton:disabled { border-color: rgba(140, 140, 140, 0.35); }"
};

// GNOME animates a window away over about 150 ms, and until it is gone it is still on screen: in the
// selector's backdrop, and in the first frames of a capture that starts behind it.
constexpr int HideSettleMilliseconds{ 250 };

constexpr std::array AudioSources{ Recorder::EAudioSource::System, Recorder::EAudioSource::Microphone };

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
Recorder::SScreen GetPrimaryScreen()
{
	QScreen const* const pScreen{ QGuiApplication::primaryScreen() };
	QSize const          size{ pScreen->geometry().size() };

	return Recorder::SScreen{ static_cast<uint32_t>(size.width()), static_cast<uint32_t>(size.height()),
		                      static_cast<uint32_t>(std::lround(pScreen->refreshRate())) };
}

//////////////////////////////////////////////////////////////////////////
// These touch a widget only when it differs: Sync runs on every meter tick, and an open combo popup must
// survive that.
void Select(QComboBox& box, QVariant const& data)
{
	int const index{ box.findData(data) };

	if (index != box.currentIndex())
	{
		QSignalBlocker const blocker{ &box };
		box.setCurrentIndex(index);
	}
}

//////////////////////////////////////////////////////////////////////////
void Check(QCheckBox& box, bool checked)
{
	if (box.isChecked() != checked)
	{
		QSignalBlocker const blocker{ &box };
		box.setChecked(checked);
	}
}

//////////////////////////////////////////////////////////////////////////
void SetValue(QSlider& slider, int value)
{
	if (slider.value() != value)
	{
		QSignalBlocker const blocker{ &slider };
		slider.setValue(value);
	}
}

//////////////////////////////////////////////////////////////////////////
void SetText(QLineEdit& edit, QString const& text)
{
	if (edit.text() != text)
	{
		edit.setText(text);
	}
}

//////////////////////////////////////////////////////////////////////////
bool SetShown(QWidget& widget, bool shown)
{
	bool const changed{ widget.isHidden() == shown };

	if (changed)
	{
		widget.setVisible(shown);
	}

	return changed;
}

//////////////////////////////////////////////////////////////////////////
void FillCodecs(QComboBox& box, std::span<Encode::ECodec const> codecs)
{
	bool same{ box.count() == static_cast<int>(codecs.size()) };

	for (size_t index{ 0 }; same && index < codecs.size(); ++index)
	{
		same = box.itemData(static_cast<int>(index)).toInt() == static_cast<int>(codecs[index]);
	}

	if (!same)
	{
		QSignalBlocker const blocker{ &box };
		box.clear();

		for (Encode::ECodec const codec : codecs)
		{
			box.addItem(ToQString(Recorder::GetCodecLabel(codec)), static_cast<int>(codec));
		}
	}
}

//////////////////////////////////////////////////////////////////////////
void FillDevices(QComboBox& box, std::span<Capture::SAudioDevice const> devices)
{
	bool same{ box.count() == static_cast<int>(devices.size()) };

	for (size_t index{ 0 }; same && index < devices.size(); ++index)
	{
		int const position{ static_cast<int>(index) };

		same = box.itemData(position).toString() == QString::fromStdString(devices[index].nodeName) &&
		       box.itemText(position) == QString::fromStdString(devices[index].description);
	}

	if (!same)
	{
		QSignalBlocker const blocker{ &box };
		box.clear();

		for (Capture::SAudioDevice const& device : devices)
		{
			box.addItem(QString::fromStdString(device.description), QString::fromStdString(device.nodeName));
		}
	}
}
} // namespace

//////////////////////////////////////////////////////////////////////////
CMainWindow::CMainWindow(QWidget* pParent)
	: QWidget(pParent)
{
}

//////////////////////////////////////////////////////////////////////////
bool CMainWindow::Initialize()
{
	m_pDeadline = new QTimer(this);
	m_pDeadline->setSingleShot(true);
	m_pDeadline->setTimerType(Qt::PreciseTimer);
	connect(m_pDeadline, &QTimer::timeout, this, &CMainWindow::OnRecorderUpdate);

	bool const initialized{ m_recorder.Initialize(
		LoadSettings(), GetPrimaryScreen(), DrawTrayIcons(),
		[this](Desktop::SRequest const& request) { Request(request); },
		[this]() {
			// Arrives on the PipeWire or the bus thread; the recorder is the UI thread's.
			QMetaObject::invokeMethod(this, [this]() { OnRecorderUpdate(); }, Qt::QueuedConnection);
		}) };

	BuildLayout();
	Sync();

	return initialized;
}

//////////////////////////////////////////////////////////////////////////
void CMainWindow::Terminate()
{
	m_recorder.Terminate();
}

//////////////////////////////////////////////////////////////////////////
void CMainWindow::BuildLayout()
{
	setWindowTitle(tr("Klip %1").arg(QStringLiteral(KLIP_VERSION)));
	setFixedWidth(WindowWidth);

	m_pSource = new QComboBox(this);

	for (Recorder::ESource const source :
	     { Recorder::ESource::Screen, Recorder::ESource::Window, Recorder::ESource::Region })
	{
		m_pSource->addItem(ToQString(Recorder::GetSourceLabel(source)), static_cast<int>(source));
	}

	m_pRememberWindow = new QCheckBox(tr("Remember the window"), this);
	m_pRememberWindow->setToolTip(
		tr("Skip the picker and record the same window again. Untick to be asked each time."));

	m_pDirectory = new QLineEdit(this);
	m_pDirectory->setReadOnly(true);

	m_pBrowse = new QPushButton(tr("Change…"), this);
	connect(m_pBrowse, &QPushButton::clicked, this, &CMainWindow::OnBrowsePressed);

	m_pOpen = new QPushButton(tr("Open"), this);
	m_pOpen->setToolTip(tr("Show the recordings in your file manager"));
	connect(m_pOpen, &QPushButton::clicked, this, &CMainWindow::OnOpenPressed);

	QHBoxLayout* const pDirectoryRow{ new QHBoxLayout };
	pDirectoryRow->addWidget(m_pDirectory, 1);
	pDirectoryRow->addWidget(m_pOpen);
	pDirectoryRow->addWidget(m_pBrowse);

	m_pContainer = new QComboBox(this);

	for (Encode::EContainer const container : m_recorder.GetContainers())
	{
		m_pContainer->addItem(ToQString(Recorder::GetContainerLabel(container)), static_cast<int>(container));
	}

	m_pCodec = new QComboBox(this);

	m_pFrameRate = new QComboBox(this);

	for (uint32_t const cap : Recorder::FrameRateCaps)
	{
		m_pFrameRate->addItem(ToQString(Recorder::GetFrameRateLabel(cap)), static_cast<int>(cap));
	}

	m_pQuality = new QComboBox(this);

	for (size_t index{ 0 }; index < Encode::QualityCount; ++index)
	{
		Encode::EQuality const quality{ static_cast<Encode::EQuality>(index) };
		m_pQuality->addItem(ToQString(Recorder::GetQualityLabel(quality)), static_cast<int>(quality));
	}

	m_pQualityHint = new QLabel(this);
	m_pQualityHint->setEnabled(false);

	QVBoxLayout* const pQualityColumn{ new QVBoxLayout };
	pQualityColumn->setContentsMargins(0, 0, 0, 0);
	pQualityColumn->setSpacing(2);
	pQualityColumn->addWidget(m_pQuality);
	pQualityColumn->addWidget(m_pQualityHint);

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
	connect(m_pRecord, &QPushButton::clicked, this, [this]() { Apply([this]() { ToggleRecording(); }); });

	m_pElapsed = new QLabel(this);
	m_pStatus = new QLabel(this);
	m_pStatus->setAlignment(Qt::AlignRight | Qt::AlignVCenter);

	QHBoxLayout* const pStatusRow{ new QHBoxLayout };
	pStatusRow->addWidget(m_pElapsed);
	pStatusRow->addWidget(m_pStatus, 1);

	BuildAudioGroup();
	ConnectInputs();

	QVBoxLayout* const pLayout{ new QVBoxLayout(this) };
	pLayout->addLayout(pForm);
	pLayout->addWidget(m_pAudioGroup);
	pLayout->addSpacing(8);
	pLayout->addWidget(m_pRecord);
	pLayout->addLayout(pStatusRow);
}

//////////////////////////////////////////////////////////////////////////
// Created kind by kind rather than source by source: the order of creation is the order of the Tab key.
void CMainWindow::BuildAudioGroup()
{
	m_pAudioGroup = new QGroupBox(tr("Audio"), this);

	m_system.pEnabled = new QCheckBox(tr("System"), m_pAudioGroup);
	m_system.pEnabled->setToolTip(tr("Record what the machine plays, whatever the speakers are set to."));

	m_microphone.pEnabled = new QCheckBox(tr("Microphone"), m_pAudioGroup);

	for (Recorder::EAudioSource const source : AudioSources)
	{
		QComboBox* const pDevice{ new QComboBox(m_pAudioGroup) };

		// A combo asks for its longest entry, and device names run well past the window's fixed width.
		pDevice->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
		pDevice->setMinimumContentsLength(16);

		GetAudioWidgets(source).pDevice = pDevice;
	}

	for (Recorder::EAudioSource const source : AudioSources)
	{
		QSlider* const pGain{ new QSlider(Qt::Horizontal, m_pAudioGroup) };
		pGain->setRange(Recorder::MinimumGainDecibels, Recorder::MaximumGainDecibels);
		pGain->setTickInterval(10);
		pGain->setToolTip(tr("Klip's own level for this source. Your system volumes are left alone."));

		GetAudioWidgets(source).pGain = pGain;
	}

	for (Recorder::EAudioSource const source : AudioSources)
	{
		QLabel* const pValue{ new QLabel(m_pAudioGroup) };
		pValue->setEnabled(false);
		pValue->setMinimumWidth(GainValueWidth);
		pValue->setAlignment(Qt::AlignRight | Qt::AlignVCenter);

		GetAudioWidgets(source).pGainValue = pValue;
	}

	m_pAudioQuality = new QComboBox(m_pAudioGroup);

	for (size_t index{ 0 }; index < Encode::QualityCount; ++index)
	{
		Encode::EQuality const quality{ static_cast<Encode::EQuality>(index) };
		m_pAudioQuality->addItem(ToQString(Recorder::GetQualityLabel(quality)), static_cast<int>(quality));
	}

	m_pAudioQualityHint = new QLabel(m_pAudioGroup);
	m_pAudioQualityHint->setEnabled(false);

	for (Recorder::EAudioSource const source : AudioSources)
	{
		CLevelMeter* const pMeter{ new CLevelMeter(m_pAudioGroup) };
		pMeter->SetBallistics(&m_recorder.GetMeter(source));

		GetAudioWidgets(source).pMeter = pMeter;
	}

	QVBoxLayout* const pQualityColumn{ new QVBoxLayout };
	pQualityColumn->setContentsMargins(0, 0, 0, 0);
	pQualityColumn->setSpacing(2);
	pQualityColumn->addWidget(m_pAudioQuality);
	pQualityColumn->addWidget(m_pAudioQualityHint);

	QFormLayout* const pAudioForm{ new QFormLayout(m_pAudioGroup) };

	for (Recorder::EAudioSource const source : AudioSources)
	{
		SAudioWidgets const& widgets{ GetAudioWidgets(source) };

		QHBoxLayout* const pGainRow{ new QHBoxLayout };
		pGainRow->setContentsMargins(0, 0, 0, 0);
		pGainRow->addWidget(widgets.pGain, 1);
		pGainRow->addWidget(widgets.pGainValue);

		pAudioForm->addRow(widgets.pEnabled, widgets.pDevice);
		pAudioForm->addRow(QString{}, pGainRow);
		pAudioForm->addRow(QString{}, widgets.pMeter);
	}

	pAudioForm->addRow(tr("Quality"), pQualityColumn);
}

//////////////////////////////////////////////////////////////////////////
// Only a user's change reaches these: Sync blocks the signals of everything it sets.
void CMainWindow::ConnectInputs()
{
	auto const apply = [this](std::function<void()> action) {
		return [this, action = std::move(action)]() { Apply(action); };
	};

	connect(m_pSource, &QComboBox::currentIndexChanged, this, apply([this]() {
		m_recorder.SetSource(static_cast<Recorder::ESource>(m_pSource->currentData().toInt()));
	}));

	connect(m_pRememberWindow, &QCheckBox::toggled, this,
	        apply([this]() { m_recorder.SetRememberWindow(m_pRememberWindow->isChecked()); }));

	connect(m_pContainer, &QComboBox::currentIndexChanged, this, apply([this]() {
		m_recorder.SetContainer(static_cast<Encode::EContainer>(m_pContainer->currentData().toInt()));
	}));

	connect(m_pCodec, &QComboBox::currentIndexChanged, this, apply([this]() {
		m_recorder.SetCodec(static_cast<Encode::ECodec>(m_pCodec->currentData().toInt()));
	}));

	connect(m_pFrameRate, &QComboBox::currentIndexChanged, this, apply([this]() {
		m_recorder.SetMaxFrameRate(static_cast<uint32_t>(m_pFrameRate->currentData().toInt()));
	}));

	connect(m_pQuality, &QComboBox::currentIndexChanged, this, apply([this]() {
		m_recorder.SetQuality(static_cast<Encode::EQuality>(m_pQuality->currentData().toInt()));
	}));

	connect(m_pAudioQuality, &QComboBox::currentIndexChanged, this, apply([this]() {
		m_recorder.SetAudioQuality(static_cast<Encode::EQuality>(m_pAudioQuality->currentData().toInt()));
	}));

	for (Recorder::EAudioSource const source : AudioSources)
	{
		SAudioWidgets const& widgets{ GetAudioWidgets(source) };

		connect(widgets.pEnabled, &QCheckBox::toggled, this, apply([this, source]() {
			m_recorder.SetAudioEnabled(source, GetAudioWidgets(source).pEnabled->isChecked());
		}));

		connect(widgets.pDevice, &QComboBox::currentIndexChanged, this, apply([this, source]() {
			m_recorder.SetAudioDevice(source, GetAudioWidgets(source).pDevice->currentData().toString().toStdString());
		}));

		connect(widgets.pGain, &QSlider::valueChanged, this, apply([this, source]() {
			m_recorder.SetGain(source, GetAudioWidgets(source).pGain->value());
		}));
	}
}

//////////////////////////////////////////////////////////////////////////
void CMainWindow::Apply(std::function<void()> const& action)
{
	// The size hint reads the screen as it is at the change, as it always has.
	m_recorder.SetScreen(GetPrimaryScreen());

	action();

	AfterChange();
}

//////////////////////////////////////////////////////////////////////////
void CMainWindow::AfterChange()
{
	Sync();

	SaveSettings(m_recorder.GetSettings(), m_recorder.TakeSettingsChanges());

	switch (m_recorder.TakeReveal())
	{
		case Recorder::EReveal::None:
			break;

		case Recorder::EReveal::Show:
			show();
			break;

		case Recorder::EReveal::Raise:
			show();
			raise();
			break;
	}

	ArmDeadline();
}

//////////////////////////////////////////////////////////////////////////
void CMainWindow::Sync()
{
	Recorder::SSettings const& settings{ m_recorder.GetSettings() };
	Recorder::EState const     state{ m_recorder.GetState() };

	bool const idle{ state == Recorder::EState::Idle };
	bool const carries{ m_recorder.CarriesAudio() };

	Select(*m_pSource, static_cast<int>(settings.source));
	Check(*m_pRememberWindow, settings.rememberWindow);
	m_pRememberWindow->setEnabled(settings.source == Recorder::ESource::Window);

	SetText(*m_pDirectory, QString::fromStdString(settings.directory));

	Select(*m_pContainer, static_cast<int>(settings.container));
	FillCodecs(*m_pCodec, m_recorder.GetCodecs());
	Select(*m_pCodec, static_cast<int>(settings.codec));
	Select(*m_pFrameRate, static_cast<int>(settings.maxFrameRate));
	Select(*m_pQuality, static_cast<int>(settings.quality));
	m_pQualityHint->setText(QString::fromStdString(m_recorder.GetQualityHint()));

	for (QWidget* const pInput : std::initializer_list<QWidget*>{ m_pSource, m_pDirectory, m_pBrowse, m_pContainer,
	                                                               m_pCodec, m_pFrameRate, m_pQuality })
	{
		pInput->setEnabled(idle);
	}

	bool const systemMoved{ SyncAudio(Recorder::EAudioSource::System, idle, carries) };
	bool const microphoneMoved{ SyncAudio(Recorder::EAudioSource::Microphone, idle, carries) };

	Select(*m_pAudioQuality, static_cast<int>(settings.audioQuality));
	m_pAudioQuality->setEnabled(idle && carries);
	m_pAudioQualityHint->setText(QString::fromStdString(m_recorder.GetAudioQualityHint()));

	QString const audioTip{ carries ? QString{}
	                                : tr("WebM carries only Opus or Vorbis, which this build cannot write. "
	                                     "Choose MP4 or Matroska to record sound.") };

	if (m_pAudioGroup->toolTip() != audioTip)
	{
		m_pAudioGroup->setToolTip(audioTip);
	}

	m_pRecord->setText(state == Recorder::EState::Recording ? tr("Stop") : tr("Record"));
	m_pRecord->setEnabled(state != Recorder::EState::Starting);
	m_pElapsed->setText(QString::fromStdString(m_recorder.GetElapsed()));
	m_pStatus->setText(QString::fromStdString(m_recorder.GetStatus()));

	if (systemMoved || microphoneMoved)
	{
		FitHeight();
	}
}

//////////////////////////////////////////////////////////////////////////
// Answers whether a row appeared or went, which changes the window's height.
bool CMainWindow::SyncAudio(Recorder::EAudioSource source, bool idle, bool carries)
{
	SAudioWidgets&                widgets{ GetAudioWidgets(source) };
	Recorder::SAudioChoice const& choice{ m_recorder.GetSettings().audio[static_cast<size_t>(source)] };

	bool const inputs{ idle && carries };
	bool const shown{ carries && choice.enabled };

	Check(*widgets.pEnabled, choice.enabled);
	widgets.pEnabled->setEnabled(inputs);

	FillDevices(*widgets.pDevice, m_recorder.GetDevices(source));
	Select(*widgets.pDevice, QString::fromStdString(choice.device));
	widgets.pDevice->setEnabled(inputs && choice.enabled);

	SetValue(*widgets.pGain, choice.gainDecibels);
	widgets.pGain->setEnabled(inputs && choice.enabled);
	widgets.pGainValue->setText(tr("%1 dB").arg(choice.gainDecibels));

	bool moved{ SetShown(*widgets.pMeter, shown) };
	moved = SetShown(*widgets.pGain, shown) || moved;
	moved = SetShown(*widgets.pGainValue, shown) || moved;

	if (shown)
	{
		widgets.pMeter->update();
	}

	return moved;
}

//////////////////////////////////////////////////////////////////////////
void CMainWindow::FitHeight()
{
	if (layout() != nullptr)
	{
		m_pAudioGroup->layout()->activate();
		setFixedHeight(sizeHint().height());
	}
}

//////////////////////////////////////////////////////////////////////////
void CMainWindow::ArmDeadline()
{
	std::optional<Recorder::CRecorder::TimePoint> const next{ m_recorder.GetNextDeadline() };

	if (next.has_value())
	{
		std::chrono::milliseconds const wait{ std::chrono::ceil<std::chrono::milliseconds>(
			*next - std::chrono::steady_clock::now()) };

		m_pDeadline->start(static_cast<int>(std::max<int64_t>(wait.count(), 0)));
	}
	else
	{
		m_pDeadline->stop();
	}
}

//////////////////////////////////////////////////////////////////////////
CMainWindow::SAudioWidgets& CMainWindow::GetAudioWidgets(Recorder::EAudioSource source)
{
	return source == Recorder::EAudioSource::System ? m_system : m_microphone;
}

//////////////////////////////////////////////////////////////////////////
void CMainWindow::OnRecorderUpdate()
{
	m_recorder.Update();

	AfterChange();
}

//////////////////////////////////////////////////////////////////////////
void CMainWindow::OnBrowsePressed()
{
	QString const chosen{ QFileDialog::getExistingDirectory(
		this, tr("Save recordings to"), QString::fromStdString(m_recorder.GetSettings().directory)) };

	if (!chosen.isEmpty())
	{
		Apply([this, &chosen]() { m_recorder.SetDirectory(chosen.toStdString()); });
	}
}

//////////////////////////////////////////////////////////////////////////
void CMainWindow::ShowInFileManager(QString const& filePath)
{
	bool shown{ false };

	if (!filePath.isEmpty())
	{
		std::string const uri{ QUrl::fromLocalFile(filePath).toString().toStdString() };

		// Selects the file rather than just opening the folder, where the desktop implements it.
		Bus::gConnection.Run([&uri, &shown](sd_bus* pBus) {
			shown = sd_bus_call_method(pBus, "org.freedesktop.FileManager1", "/org/freedesktop/FileManager1",
			                           "org.freedesktop.FileManager1", "ShowItems", nullptr, nullptr, "ass", 1,
			                           uri.c_str(), "")
			        >= 0;
		});
	}

	std::string const& directory{ m_recorder.GetSettings().directory };

	if (!shown && !QDesktopServices::openUrl(QUrl::fromLocalFile(QString::fromStdString(directory))))
	{
		gLog.Error("Could not open {}", directory);
	}
}

//////////////////////////////////////////////////////////////////////////
void CMainWindow::OnOpenPressed()
{
	ShowInFileManager(m_recorder.IsEncoding() ? QString{} : QString::fromStdString(m_recorder.GetLastPath()));
}

//////////////////////////////////////////////////////////////////////////
void CMainWindow::ToggleRecording()
{
	if (m_recorder.IsEncoding())
	{
		m_recorder.StopRecording();
	}
	else if (m_recorder.PrepareRecording())
	{
		std::optional<Encode::SRegion> region{ Encode::SRegion{} };

		if (m_recorder.GetSettings().source == Recorder::ESource::Region)
		{
			region = ChooseRegion();
		}

		if (region.has_value())
		{
			m_recorder.BeginRecording(*region);

			// Before the window goes, and for as long as it stays when there is no tray to hide in.
			Sync();

			// Out of shot before the stream opens; hiding once it runs films the window fading out.
			if (isVisible() && m_recorder.IsTrayAvailable())
			{
				hide();
				SettleAfterHiding();
			}

			m_recorder.RequestCapture();
		}
		else
		{
			show();
			m_recorder.CancelRecording();
		}
	}
}

//////////////////////////////////////////////////////////////////////////
std::optional<Encode::SRegion> CMainWindow::ChooseRegion()
{
	hide();
	SettleAfterHiding();

	CRegionSelector selector;
	QRect const     chosen{ selector.Choose() };

	std::optional<Encode::SRegion> region;

	if (!chosen.isEmpty())
	{
		region = Encode::SRegion{ static_cast<uint32_t>(chosen.x()), static_cast<uint32_t>(chosen.y()),
			                      static_cast<uint32_t>(chosen.width()), static_cast<uint32_t>(chosen.height()) };

		SettleAfterHiding();
	}

	return region;
}

//////////////////////////////////////////////////////////////////////////
void CMainWindow::Reveal()
{
	showNormal();
	raise();
	activateWindow();
}

//////////////////////////////////////////////////////////////////////////
void CMainWindow::Request(Desktop::SRequest const& request)
{
	// The request crosses through the queue; the wake carries nothing.
	m_requests.Enqueue(request);
	QMetaObject::invokeMethod(this, [this]() { OnDesktopRequests(); }, Qt::QueuedConnection);
}

//////////////////////////////////////////////////////////////////////////
void CMainWindow::OnDesktopRequests()
{
	Desktop::SRequest request;
	bool quitting{ false };

	// Nothing after a Quit: a Toggle behind it would open a portal request on the way out.
	while (!quitting && m_requests.Dequeue(request))
	{
		switch (request.kind)
		{
			case Desktop::ERequest::ActivationToken:
				// The compositor refuses a raise it did not sanction, and Qt's Wayland plugin looks here for
				// the token that sanctions this one.
				qputenv("XDG_ACTIVATION_TOKEN", QByteArray::fromStdString(request.token));
				break;

			case Desktop::ERequest::Show:
				Reveal();
				break;

			case Desktop::ERequest::Toggle:
				Apply([this]() { ToggleRecording(); });
				break;

			case Desktop::ERequest::Quit:
				quitting = true;
				qApp->quit();
				break;
		}
	}
}

//////////////////////////////////////////////////////////////////////////
void CMainWindow::closeEvent(QCloseEvent* pEvent)
{
	if (m_recorder.IsTrayAvailable())
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
// isVisible() rather than true or false: on X11 a minimise sends a hide event to a window that still reads
// as visible, and the restore a show event.
void CMainWindow::showEvent(QShowEvent* pEvent)
{
	QWidget::showEvent(pEvent);

	m_recorder.RefreshAudioDevices();
	m_recorder.SetVisible(isVisible());

	AfterChange();
	FitHeight();
}

//////////////////////////////////////////////////////////////////////////
void CMainWindow::hideEvent(QHideEvent* pEvent)
{
	QWidget::hideEvent(pEvent);

	m_recorder.SetVisible(isVisible());

	AfterChange();
}
} // namespace Klip
