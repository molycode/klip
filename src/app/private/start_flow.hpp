#pragma once

#include "capture/screenshot.hpp"
#include "encode/settings.hpp"
#include "region_selector.hpp"
#include "start_step.hpp"

#include <tge/non_copyable.hpp>

#include <chrono>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

struct SDL_Window;

union SDL_Event;

namespace Klip
{
namespace Recorder
{
class CRecorder;
} // namespace Recorder

class CStartFlow final : private Tge::SNoCopyNoMove
{
public:

	using TimePoint    = std::chrono::steady_clock::time_point;
	using WakeCallback = std::function<void()>;

	CStartFlow() = default;
	~CStartFlow() = default;

	void Initialize(SDL_Window* pMainWindow, Recorder::CRecorder& recorder, WakeCallback wake);
	void Terminate();

	void Toggle(std::string_view activationToken);

	void Update(TimePoint now);
	std::optional<TimePoint> GetNextDeadline() const;

	bool IsSelecting() const { return m_selector.IsOpen(); }
	bool OwnsEvent(SDL_Event const& event) const { return m_selector.OwnsEvent(event); }
	void ProcessEvent(SDL_Event const& event) { m_selector.ProcessEvent(event); }
	void DrawSelector() { m_selector.Draw(); }

private:

	void Settle(EStartStep next, TimePoint now);
	void OpenSelector(std::string const& backdropPath);
	void Begin(Encode::SRegion const& region, TimePoint now);
	void Cancel();

	bool IsMainShown() const;

	SDL_Window*          m_pMainWindow{ nullptr };
	Recorder::CRecorder* m_pRecorder{ nullptr };
	WakeCallback         m_wake;

	Capture::CScreenshot m_screenshot;
	CRegionSelector      m_selector;

	std::string              m_activationToken;
	std::optional<TimePoint> m_deadline;
	EStartStep               m_step{ EStartStep::None };
};
} // namespace Klip
