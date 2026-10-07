#pragma once

#include "about_dialog.hpp"
#include "config/settings_store.hpp"
#include "desktop/request.hpp"
#include "main_view.hpp"
#include "recorder/recorder.hpp"
#include "recorder/reveal.hpp"
#include "start_flow.hpp"
#include "view_intents.hpp"

#include <tge/non_copyable.hpp>
#include <tge/threading/mpsc_queue.hpp>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <string>

struct SDL_Renderer;
struct SDL_Window;

union SDL_Event;

namespace Klip
{
class CApplication final : private Tge::SNoCopyNoMove
{
public:

	CApplication() = default;
	~CApplication() = default;

	bool Initialize(std::filesystem::path const& logsDir);
	void Terminate();

	void Request(Desktop::SRequest const& request);

	void Run();

private:

	using TimePoint = std::chrono::steady_clock::time_point;

	bool CreateWindowAndRenderer();
	bool InitializeImGui();
	bool InitializeRecorder();
	bool ShowMainWindow();

	void Wake() const;
	void WaitForEvents();
	void ProcessEvent(SDL_Event const& event);
	bool TakeRequests();
	void TakeFolderAnswer();
	void SyncVisibility();
	void DrawMainFrame(bool present);
	SViewIntents DrawMainWindow(float& desiredHeight);
	void FitHeight(float desiredHeight);
	void Apply(SViewIntents const& intents);
	void OpenInFileManager();
	void SaveSettings(bool isFinal);
	void Reveal(Recorder::EReveal reveal);
	void UpdateScale();

	bool IsMainDrawable() const;
	int  GetWindowWidth() const;

	Config::CSettingsStore m_settingsStore;
	Recorder::CRecorder    m_recorder;
	CMainView              m_mainView;
	CAboutDialog           m_aboutDialog;
	CStartFlow             m_startFlow;

	Tge::Threading::CMpscQueue<Desktop::SRequest> m_requests;

	SDL_Window*   m_pWindow{ nullptr };
	SDL_Renderer* m_pRenderer{ nullptr };

	std::string m_activationToken;

	TimePoint m_activeUntil{};
	uint32_t  m_wakeEventType{ 0 };
	int       m_requestedHeight{ 0 };
	float     m_scale{ 1.0f };
	bool      m_hasVsync{ false };
	bool      m_isImGuiInitialized{ false };
	bool      m_isVisible{ false };
	bool      m_isQuitting{ false };
	bool      m_hasReportedRenderFailure{ false };
};
} // namespace Klip
