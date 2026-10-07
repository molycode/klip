#include "application.hpp"

#include "activation.hpp"
#include "bus/file_uri.hpp"
#include "config/xdg_paths.hpp"
#include "desktop/file_manager.hpp"
#include "desktop/tray_icons.hpp"
#include "folder_dialog.hpp"
#include "fonts.hpp"
#include "log.hpp"
#include "primary_display.hpp"
#include "recorder/settings.hpp"
#include "recorder/state.hpp"
#include "theme.hpp"
#include "ui_scale.hpp"

#include <SDL3/SDL.h>
#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_sdlrenderer3.h>
#include <tge/assert.hpp>

#include <algorithm>
#include <cmath>
#include <optional>
#include <string_view>

namespace Klip
{
namespace
{
constexpr float WindowWidth{ 600.0f };
constexpr float ScaleTolerance{ 0.01f };

// ImGui settles hover and focus over a few frames after the input that changed them.
constexpr std::chrono::milliseconds ActiveDuration{ 250 };
constexpr std::chrono::milliseconds UnsyncedFrameInterval{ 16 };
// The folder dialog's answer arrives over D-Bus, which SDL reads only while pumping events.
constexpr std::chrono::milliseconds DialogPollInterval{ 100 };
constexpr std::chrono::milliseconds NoTimeout{ -1 };

constexpr SDL_WindowFlags HiddenFromView{ SDL_WINDOW_HIDDEN | SDL_WINDOW_MINIMIZED | SDL_WINDOW_OCCLUDED };

//////////////////////////////////////////////////////////////////////////
// An SDL_* variable in the environment wins over the hint, as whoever set it meant it to.
void SetHint(char const* pName, char const* pValue)
{
	if (!SDL_SetHint(pName, pValue) && SDL_getenv(pName) == nullptr)
	{
		gLog.Warning("Cannot set SDL's {} to '{}': {}", pName, pValue, SDL_GetError());
	}
}

//////////////////////////////////////////////////////////////////////////
std::optional<std::chrono::steady_clock::time_point> GetEarliest(
	std::optional<std::chrono::steady_clock::time_point> first,
	std::optional<std::chrono::steady_clock::time_point> second)
{
	return (first.has_value() && second.has_value()) ? std::min(*first, *second)
	                                                 : (first.has_value() ? first : second);
}

//////////////////////////////////////////////////////////////////////////
std::chrono::milliseconds BoundByDeadline(std::chrono::milliseconds wait,
                                          std::optional<std::chrono::steady_clock::time_point> deadline)
{
	std::chrono::milliseconds bounded{ wait };

	if (deadline.has_value())
	{
		std::chrono::milliseconds const untilDeadline{ std::max(
			std::chrono::ceil<std::chrono::milliseconds>(*deadline - std::chrono::steady_clock::now()),
			std::chrono::milliseconds{ 0 }) };

		bounded = (wait == NoTimeout) ? untilDeadline : std::min(wait, untilDeadline);
	}

	return bounded;
}

//////////////////////////////////////////////////////////////////////////
std::string_view OrNone(char const* pText)
{
	return (pText != nullptr) ? std::string_view{ pText } : std::string_view{ "none" };
}
} // namespace

//////////////////////////////////////////////////////////////////////////
bool CApplication::Initialize(std::filesystem::path const& logsDir)
{
	// The region selector closes while the main window hides, which SDL would take for the last window closing.
	SetHint(SDL_HINT_QUIT_ON_LAST_WINDOW_CLOSE, "0");
	// SDL 3.4 prefers XWayland on a compositor without fifo-v1, GNOME 46 among them.
	SetHint(SDL_HINT_VIDEO_DRIVER, "wayland,x11");
	// SDL keeps the screen awake by default, and Klip sits in the tray for hours.
	SetHint(SDL_HINT_VIDEO_ALLOW_SCREENSAVER, "1");

	// The identifier is the Wayland app_id, which the compositor matches to klip.desktop for the icon.
	if (!SDL_SetAppMetadata("Klip", KLIP_VERSION, "klip"))
	{
		gLog.Warning("Cannot set the application metadata: {}", SDL_GetError());
	}

	bool initialized{ false };

	if (SDL_Init(SDL_INIT_VIDEO))
	{
		m_wakeEventType = SDL_RegisterEvents(1);

		if (m_wakeEventType == 0)
		{
			gLog.Error("Cannot register the event that wakes the window: {}", SDL_GetError());
		}

		initialized = m_wakeEventType != 0 && CreateWindowAndRenderer() && InitializeImGui() && InitializeRecorder();

		if (initialized)
		{
			m_aboutDialog.Initialize(m_pWindow, m_settingsStore.GetDirectory().string(), logsDir.string());
			initialized = ShowMainWindow();
		}
	}
	else
	{
		gLog.Error("Cannot initialize SDL video: {}", SDL_GetError());
	}

	return initialized;
}

//////////////////////////////////////////////////////////////////////////
void CApplication::Terminate()
{
	m_startFlow.Terminate();
	m_recorder.Terminate();

	if (m_isImGuiInitialized)
	{
		SaveSettings(true);

		ImGui_ImplSDLRenderer3_Shutdown();
		ImGui_ImplSDL3_Shutdown();
		ImGui::DestroyContext();
		m_isImGuiInitialized = false;
	}

	if (m_pRenderer != nullptr)
	{
		SDL_DestroyRenderer(m_pRenderer);
		m_pRenderer = nullptr;
	}

	if (m_pWindow != nullptr)
	{
		SDL_DestroyWindow(m_pWindow);
		m_pWindow = nullptr;
	}

	SDL_Quit();
}

//////////////////////////////////////////////////////////////////////////
// From any thread: the request crosses through the queue, and the wake carries nothing.
void CApplication::Request(Desktop::SRequest const& request)
{
	m_requests.Enqueue(request);
	Wake();
}

//////////////////////////////////////////////////////////////////////////
void CApplication::Run()
{
	m_activeUntil = std::chrono::steady_clock::now() + ActiveDuration;

	while (!m_isQuitting)
	{
		WaitForEvents();

		bool const isTokenTaken{ TakeRequests() };

		if (!m_isQuitting)
		{
			m_recorder.Update();
			TakeFolderAnswer();
			m_startFlow.Update(std::chrono::steady_clock::now());
			SyncVisibility();

			if (m_startFlow.IsSelecting())
			{
				m_startFlow.DrawSelector();
			}

			if (IsMainDrawable())
			{
				DrawMainFrame(true);
			}

			SaveSettings(false);
			Reveal(m_recorder.TakeReveal());
		}

		// A token arrives in a call of its own, and the request that spends it may not have arrived yet.
		if (isTokenTaken)
		{
			m_activationToken.clear();
		}
	}
}

//////////////////////////////////////////////////////////////////////////
bool CApplication::CreateWindowAndRenderer()
{
	m_pWindow = SDL_CreateWindow("Klip", static_cast<int>(WindowWidth), static_cast<int>(WindowWidth),
	                             SDL_WINDOW_HIGH_PIXEL_DENSITY | SDL_WINDOW_HIDDEN);

	if (m_pWindow != nullptr)
	{
		m_pRenderer = SDL_CreateRenderer(m_pWindow, nullptr);

		if (m_pRenderer != nullptr)
		{
			m_hasVsync = SDL_SetRenderVSync(m_pRenderer, 1);

			if (!m_hasVsync)
			{
				gLog.Warning("Cannot enable vsync, frames are paced by the event timeout only: {}", SDL_GetError());
			}
		}
		else
		{
			gLog.Error("Cannot create a renderer: {}", SDL_GetError());
		}
	}
	else
	{
		gLog.Error("Cannot create the window: {}", SDL_GetError());
	}

	return m_pRenderer != nullptr;
}

//////////////////////////////////////////////////////////////////////////
bool CApplication::InitializeImGui()
{
	IMGUI_CHECKVERSION();
	ImGui::CreateContext();

	ImGuiIO& io{ ImGui::GetIO() };

	io.IniFilename = nullptr;
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

	m_scale = ReadUiScale(m_pWindow);
	ApplyTheme(m_scale);

	bool const hasFonts{ LoadFonts() };

	if (hasFonts && ImGui_ImplSDL3_InitForSDLRenderer(m_pWindow, m_pRenderer))
	{
		if (ImGui_ImplSDLRenderer3_Init(m_pRenderer))
		{
			m_isImGuiInitialized = true;
		}
		else
		{
			gLog.Error("Cannot initialize ImGui's SDL renderer backend");
			ImGui_ImplSDL3_Shutdown();
		}
	}
	else if (hasFonts)
	{
		gLog.Error("Cannot initialize ImGui's SDL platform backend");
	}

	if (!m_isImGuiInitialized)
	{
		ImGui::DestroyContext();
	}

	return m_isImGuiInitialized;
}

//////////////////////////////////////////////////////////////////////////
// After SDL, whose display the size hint reads, and before the window shows, whose first frame draws the recorder.
bool CApplication::InitializeRecorder()
{
	m_settingsStore.Initialize(Config::GetConfigHome(), Config::GetHome());
	m_startFlow.Initialize(m_pWindow, m_recorder, [this]() { Wake(); });

	return m_recorder.Initialize(
		m_settingsStore.Load(), GetPrimaryScreen(), Desktop::DrawTrayIcons(),
		[this](Desktop::SRequest const& request) { Request(request); }, [this]() { Wake(); });
}

//////////////////////////////////////////////////////////////////////////
bool CApplication::ShowMainWindow()
{
	// Unseen, so the window opens at the height of what it holds.
	DrawMainFrame(false);
	// A launcher's activation token is in the environment SDL copied at startup, and this show spends it.
	ShowHiddenWindow(m_pWindow, {});
	SyncVisibility();

	bool const isShown{ m_isVisible };

	if (isShown)
	{
		gLog.Info("Display: SDL {}.{}.{}, video driver '{}', renderer '{}', display scale {:.2f}, pixel density {:.2f}, "
		          "UI scale {:.2f}",
		          SDL_MAJOR_VERSION, SDL_MINOR_VERSION, SDL_MICRO_VERSION, OrNone(SDL_GetCurrentVideoDriver()),
		          OrNone(SDL_GetRendererName(m_pRenderer)), SDL_GetWindowDisplayScale(m_pWindow),
		          SDL_GetWindowPixelDensity(m_pWindow), m_scale);
	}

	return isShown;
}

//////////////////////////////////////////////////////////////////////////
// From any thread.
void CApplication::Wake() const
{
	TGE_ASSERT(m_wakeEventType != 0, "The window is woken before Initialize registered its event");

	SDL_Event event{};

	event.type = m_wakeEventType;

	if (!SDL_PushEvent(&event))
	{
		gLog.Warning("Cannot wake the window: {}", SDL_GetError());
	}
}

//////////////////////////////////////////////////////////////////////////
// Input opens a burst of frames while something is on screen; a wake or a deadline draws one.
void CApplication::WaitForEvents()
{
	bool const isDrawable{ IsMainDrawable() || m_startFlow.IsSelecting() };
	bool const isActive{ isDrawable && std::chrono::steady_clock::now() < m_activeUntil };

	std::chrono::milliseconds const frameWait{ m_hasVsync ? std::chrono::milliseconds{ 0 } : UnsyncedFrameInterval };
	std::chrono::milliseconds const idleWait{ BoundByDeadline(
		IsFolderDialogPending() ? DialogPollInterval : NoTimeout,
		GetEarliest(m_recorder.GetNextDeadline(), m_startFlow.GetNextDeadline())) };
	std::chrono::milliseconds const wait{ isActive ? frameWait : idleWait };

	SDL_Event event{};
	bool      hasEvent{ SDL_WaitEventTimeout(&event, static_cast<Sint32>(wait.count())) };

	if (wait == NoTimeout && !hasEvent)
	{
		gLog.Error("Waiting for events failed, quitting: {}", SDL_GetError());
		m_isQuitting = true;
	}

	while (hasEvent)
	{
		ProcessEvent(event);
		hasEvent = SDL_PollEvent(&event);
	}
}

//////////////////////////////////////////////////////////////////////////
void CApplication::ProcessEvent(SDL_Event const& event)
{
	bool const isMainWindow{ SDL_GetWindowFromEvent(&event) == m_pWindow };

	if (m_startFlow.OwnsEvent(event))
	{
		m_startFlow.ProcessEvent(event);
	}
	else
	{
		ImGui_ImplSDL3_ProcessEvent(&event);
	}

	if (event.type != m_wakeEventType)
	{
		m_activeUntil = std::chrono::steady_clock::now() + ActiveDuration;
	}

	if (event.type >= SDL_EVENT_DISPLAY_FIRST && event.type <= SDL_EVENT_DISPLAY_LAST)
	{
		m_recorder.SetScreen(GetPrimaryScreen());
	}

	switch (event.type)
	{
		case SDL_EVENT_QUIT:
			m_isQuitting = true;
			break;

		case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
			if (isMainWindow && m_recorder.IsTrayAvailable())
			{
				SDL_HideWindow(m_pWindow);
			}
			else if (isMainWindow)
			{
				m_isQuitting = true;
			}
			break;

		// The device list goes stale while nobody looks at it.
		case SDL_EVENT_WINDOW_SHOWN:
		case SDL_EVENT_WINDOW_RESTORED:
			if (isMainWindow)
			{
				m_recorder.RefreshAudioDevices();
			}
			break;

		case SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED:
			if (isMainWindow)
			{
				UpdateScale();
			}
			break;

		default:
			break;
	}
}

//////////////////////////////////////////////////////////////////////////
// Nothing after a Quit: a Toggle behind it would open a portal request on the way out.
bool CApplication::TakeRequests()
{
	Desktop::SRequest request{};
	bool              isTokenTaken{ false };

	while (!m_isQuitting && m_requests.Dequeue(request))
	{
		isTokenTaken = isTokenTaken || request.kind != Desktop::ERequest::ActivationToken;

		switch (request.kind)
		{
			case Desktop::ERequest::ActivationToken:
				m_activationToken = request.token;
				break;

			case Desktop::ERequest::Show:
				PresentWindow(m_pWindow, std::exchange(m_activationToken, {}));
				break;

			// The token stays for the iteration: a stop's raise spends it, and a region's selector does.
			case Desktop::ERequest::Toggle:
				m_startFlow.Toggle(m_activationToken);
				break;

			case Desktop::ERequest::Quit:
				m_isQuitting = true;
				break;
		}
	}

	return isTokenTaken;
}

//////////////////////////////////////////////////////////////////////////
void CApplication::TakeFolderAnswer()
{
	std::optional<SFolderDialogResult> const result{ TakeFolderDialogResult() };

	if (result.has_value())
	{
		if (result->state == EFolderDialogState::Picked)
		{
			m_recorder.SetDirectory(result->text);
		}
		else if (result->state == EFolderDialogState::Failed)
		{
			gLog.Error("Cannot show the folder dialog: {}", result->text);
		}

		// The layout changes with the answer and settles a frame later.
		m_activeUntil = std::chrono::steady_clock::now() + ActiveDuration;
	}
}

//////////////////////////////////////////////////////////////////////////
// Minimised still counts as visible, as it always has: the previews keep running behind the taskbar.
void CApplication::SyncVisibility()
{
	bool const isVisible{ (SDL_GetWindowFlags(m_pWindow) & SDL_WINDOW_HIDDEN) == 0 };

	if (isVisible != m_isVisible)
	{
		m_isVisible = isVisible;
		m_recorder.SetVisible(isVisible);
	}
}

//////////////////////////////////////////////////////////////////////////
void CApplication::DrawMainFrame(bool present)
{
	ImGui_ImplSDLRenderer3_NewFrame();
	ImGui_ImplSDL3_NewFrame();
	ImGui::NewFrame();

	float              desiredHeight{ 0.0f };
	SViewIntents const intents{ DrawMainWindow(desiredHeight) };

	ImGui::Render();

	if (present)
	{
		ImGuiIO const& io{ ImGui::GetIO() };
		ImVec4 const   background{ GetBackgroundColor() };

		bool rendered{ SDL_SetRenderScale(m_pRenderer, io.DisplayFramebufferScale.x, io.DisplayFramebufferScale.y) };

		rendered = SDL_SetRenderDrawColorFloat(m_pRenderer, background.x, background.y, background.z, background.w) &&
		           rendered;
		rendered = SDL_RenderClear(m_pRenderer) && rendered;
		ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), m_pRenderer);
		rendered = SDL_RenderPresent(m_pRenderer) && rendered;

		// Once: a failure that recurs every frame would otherwise bury the log.
		if (!rendered && !m_hasReportedRenderFailure)
		{
			gLog.Error("Drawing a frame failed: {}", SDL_GetError());
			m_hasReportedRenderFailure = true;
		}
	}

	FitHeight(desiredHeight);
	Apply(intents);
}

//////////////////////////////////////////////////////////////////////////
SViewIntents CApplication::DrawMainWindow(float& desiredHeight)
{
	constexpr ImGuiWindowFlags Flags{ ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
		                              ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus |
		                              ImGuiWindowFlags_MenuBar };

	SViewIntents          intents{};
	ImGuiViewport const* const pViewport{ ImGui::GetMainViewport() };

	ImGui::SetNextWindowPos(pViewport->WorkPos);
	ImGui::SetNextWindowSize(pViewport->WorkSize);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);

	bool const isOpen{ ImGui::Begin("Klip", nullptr, Flags) };

	ImGui::PopStyleVar();

	if (isOpen)
	{
		ImGuiStyle const& style{ ImGui::GetStyle() };

		intents = m_mainView.Draw(m_recorder, m_scale, IsFolderDialogPending());
		desiredHeight = ImGui::GetCursorPosY() - style.ItemSpacing.y + style.WindowPadding.y;
		m_aboutDialog.Draw();
	}

	ImGui::End();

	return intents;
}

//////////////////////////////////////////////////////////////////////////
// Only when the height changes: Wayland resizes asynchronously, so the size read back lags the request.
void CApplication::FitHeight(float desiredHeight)
{
	int const height{ static_cast<int>(std::ceil(desiredHeight)) };

	if (height > 0 && height != m_requestedHeight)
	{
		m_requestedHeight = height;

		if (!SDL_SetWindowSize(m_pWindow, GetWindowWidth(), height))
		{
			gLog.Warning("Cannot fit the window to what it shows: {}", SDL_GetError());
		}
	}
}

//////////////////////////////////////////////////////////////////////////
void CApplication::Apply(SViewIntents const& intents)
{
	if (intents.toggle)
	{
		m_startFlow.Toggle({});
	}

	if (intents.browse)
	{
		OpenFolderDialog(m_pWindow, m_recorder.GetSettings().directory.c_str());
	}

	if (intents.open)
	{
		OpenInFileManager();
	}

	if (intents.about)
	{
		m_aboutDialog.Open();
	}

	if (intents.quit)
	{
		m_isQuitting = true;
	}
}

//////////////////////////////////////////////////////////////////////////
// The last recording, selected, once there is one and nothing is being written; otherwise its folder.
void CApplication::OpenInFileManager()
{
	std::string const& lastPath{ m_recorder.GetLastPath() };
	std::string const& directory{ m_recorder.GetSettings().directory };

	bool const shown{ m_recorder.GetState() == Recorder::EState::Idle && !lastPath.empty() &&
		              Desktop::ShowInFileManager(Bus::ToFileUri(lastPath)) };

	if (!shown && !SDL_OpenURL(Bus::ToFileUri(directory).c_str()))
	{
		gLog.Error("Could not open {}: {}", directory, SDL_GetError());
	}
}

//////////////////////////////////////////////////////////////////////////
// Not while the mouse is held, so a slider drag writes once, when it is let go. Not on IsAnyItemActive: a
// clicked text field stays active until the next click, through any time spent hidden.
void CApplication::SaveSettings(bool isFinal)
{
	if (isFinal || !ImGui::IsMouseDown(ImGuiMouseButton_Left))
	{
		m_settingsStore.Save(m_recorder.GetSettings(), m_recorder.TakeSettingsChanges());
	}
}

//////////////////////////////////////////////////////////////////////////
void CApplication::Reveal(Recorder::EReveal reveal)
{
	switch (reveal)
	{
		case Recorder::EReveal::None:
			break;

		case Recorder::EReveal::Show:
			ShowHiddenWindow(m_pWindow, m_activationToken);
			break;

		case Recorder::EReveal::Raise:
			PresentWindow(m_pWindow, m_activationToken);
			break;
	}
}

//////////////////////////////////////////////////////////////////////////
void CApplication::UpdateScale()
{
	float const scale{ ReadUiScale(m_pWindow) };

	if (std::abs(scale - m_scale) > ScaleTolerance)
	{
		m_scale = scale;
		m_requestedHeight = 0;
		ApplyTheme(m_scale);
		gLog.Info("UI scale changed to {:.2f}", m_scale);
	}
}

//////////////////////////////////////////////////////////////////////////
bool CApplication::IsMainDrawable() const
{
	return (SDL_GetWindowFlags(m_pWindow) & HiddenFromView) == 0;
}

//////////////////////////////////////////////////////////////////////////
int CApplication::GetWindowWidth() const
{
	return static_cast<int>(std::lround(WindowWidth * m_scale));
}
} // namespace Klip
