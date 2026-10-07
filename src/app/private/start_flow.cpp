#include "start_flow.hpp"

#include "activation.hpp"
#include "log.hpp"
#include "recorder/recorder.hpp"
#include "recorder/source.hpp"
#include "recorder/state.hpp"

#include <SDL3/SDL.h>
#include <tge/assert.hpp>

#include <filesystem>
#include <system_error>
#include <utility>

namespace Klip
{
namespace
{
// GNOME animates a window away over about 150 ms, and until it is gone it is still on screen: in the
// selector's backdrop, and in the first frames of a capture that starts behind it.
constexpr std::chrono::milliseconds HideSettle{ 250 };

// The portal puts a permission dialog in front of the first request a user ever makes.
constexpr std::chrono::seconds ScreenshotPatience{ 30 };

//////////////////////////////////////////////////////////////////////////
// The portal writes one file per request and hands it over, so it is deleted once read.
SDL_Surface* LoadBackdrop(std::string const& path)
{
	SDL_Surface* pBackdrop{ nullptr };

	if (!path.empty())
	{
		std::error_code error{};

		pBackdrop = SDL_LoadPNG(path.c_str());

		if (pBackdrop == nullptr)
		{
			gLog.Error("Cannot read the screenshot at {}: {}", path, SDL_GetError());
		}

		std::filesystem::remove(path, error);

		if (error.value() != 0)
		{
			gLog.Warning("Cannot delete the screenshot at {}: {}", path, error.message());
		}
	}

	return pBackdrop;
}
} // namespace

//////////////////////////////////////////////////////////////////////////
void CStartFlow::Initialize(SDL_Window* pMainWindow, Recorder::CRecorder& recorder, WakeCallback wake)
{
	m_pMainWindow = pMainWindow;
	m_pRecorder = &recorder;
	m_wake = std::move(wake);
}

//////////////////////////////////////////////////////////////////////////
void CStartFlow::Terminate()
{
	if (m_step != EStartStep::None)
	{
		m_screenshot.Cancel();
		m_selector.Close();
		m_step = EStartStep::None;
		m_deadline.reset();
	}
}

//////////////////////////////////////////////////////////////////////////
// Dropped while a start is on its way: it would open a second portal request over the first.
void CStartFlow::Toggle(std::string_view activationToken)
{
	TGE_ASSERT(m_pRecorder != nullptr, "The start flow toggles before Initialize");

	if (m_pRecorder->GetState() == Recorder::EState::Recording)
	{
		m_pRecorder->StopRecording();
	}
	else if (m_step == EStartStep::None && m_pRecorder->PrepareRecording())
	{
		TimePoint const now{ std::chrono::steady_clock::now() };

		if (m_pRecorder->GetSettings().source == Recorder::ESource::Region)
		{
			m_activationToken = activationToken;
			SDL_HideWindow(m_pMainWindow);
			Settle(EStartStep::SettlingBeforeBackdrop, now);
		}
		else
		{
			Begin(Encode::SRegion{}, now);
		}
	}
}

//////////////////////////////////////////////////////////////////////////
// Every wait is a deadline or an answer, so the window keeps drawing throughout a start.
void CStartFlow::Update(TimePoint now)
{
	bool const isDue{ m_deadline.has_value() && now >= *m_deadline };

	switch (m_step)
	{
		case EStartStep::None:
			break;

		case EStartStep::SettlingBeforeBackdrop:
			if (isDue)
			{
				m_deadline.reset();

				if (m_screenshot.Request(m_wake))
				{
					m_step = EStartStep::AwaitingBackdrop;
					m_deadline = now + ScreenshotPatience;
				}
				else
				{
					OpenSelector({});
				}
			}
			break;

		case EStartStep::AwaitingBackdrop:
			if (std::optional<std::string> const answer{ m_screenshot.TakeAnswer() }; answer.has_value())
			{
				OpenSelector(*answer);
			}
			else if (isDue)
			{
				gLog.Error("The screenshot portal never answered.");
				m_screenshot.Cancel();
				OpenSelector({});
			}
			break;

		case EStartStep::Picking:
			if (m_selector.GetAnswer() == ESelectorAnswer::Accepted)
			{
				m_selector.Close();
				Settle(EStartStep::SettlingAfterPicker, now);
			}
			else if (m_selector.GetAnswer() == ESelectorAnswer::Cancelled)
			{
				m_selector.Close();
				Cancel();
			}
			break;

		case EStartStep::SettlingAfterPicker:
			if (isDue)
			{
				Begin(m_selector.GetRegion(), now);
			}
			break;

		case EStartStep::SettlingBeforeCapture:
			if (isDue)
			{
				m_step = EStartStep::None;
				m_deadline.reset();
				m_pRecorder->RequestCapture();
			}
			break;
	}
}

//////////////////////////////////////////////////////////////////////////
std::optional<CStartFlow::TimePoint> CStartFlow::GetNextDeadline() const
{
	return m_deadline;
}

//////////////////////////////////////////////////////////////////////////
void CStartFlow::Settle(EStartStep next, TimePoint now)
{
	m_step = next;
	m_deadline = now + HideSettle;
}

//////////////////////////////////////////////////////////////////////////
// Without a backdrop the selector still opens, so a region can be dragged over the dark.
void CStartFlow::OpenSelector(std::string const& backdropPath)
{
	m_deadline.reset();

	if (m_selector.Open(LoadBackdrop(backdropPath), std::exchange(m_activationToken, {})))
	{
		m_step = EStartStep::Picking;
	}
	else
	{
		Cancel();
	}
}

//////////////////////////////////////////////////////////////////////////
// Out of shot before the stream opens; hiding once it runs films the window fading out.
void CStartFlow::Begin(Encode::SRegion const& region, TimePoint now)
{
	m_step = EStartStep::None;
	m_deadline.reset();
	m_pRecorder->BeginRecording(region);

	if (IsMainShown() && m_pRecorder->IsTrayAvailable())
	{
		SDL_HideWindow(m_pMainWindow);
		Settle(EStartStep::SettlingBeforeCapture, now);
	}
	else
	{
		m_pRecorder->RequestCapture();
	}
}

//////////////////////////////////////////////////////////////////////////
void CStartFlow::Cancel()
{
	m_step = EStartStep::None;
	m_deadline.reset();
	ShowHiddenWindow(m_pMainWindow, {});
	m_pRecorder->CancelRecording();
}

//////////////////////////////////////////////////////////////////////////
bool CStartFlow::IsMainShown() const
{
	return (SDL_GetWindowFlags(m_pMainWindow) & SDL_WINDOW_HIDDEN) == 0;
}
} // namespace Klip
