#include "region_selector.hpp"

#include "activation.hpp"
#include "fonts.hpp"
#include "format_to.hpp"
#include "log.hpp"
#include "theme.hpp"
#include "ui_scale.hpp"

#include <SDL3/SDL.h>
#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_sdlrenderer3.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <string_view>

namespace Klip
{
namespace
{
constexpr int   MinimumSide{ 16 };
constexpr float LabelMargin{ 8.0f };
constexpr float BorderWidth{ 2.0f };
constexpr ImU32 ShadeColor{ IM_COL32(0, 0, 0, 110) };
constexpr ImU32 LabelColor{ IM_COL32(240, 240, 240, 255) };

constexpr SDL_WindowFlags WindowFlags{ SDL_WINDOW_FULLSCREEN | SDL_WINDOW_BORDERLESS |
	                                   SDL_WINDOW_HIGH_PIXEL_DENSITY | SDL_WINDOW_ALWAYS_ON_TOP | SDL_WINDOW_HIDDEN };

struct SPixelSpan final
{
	int first{ 0 };
	int count{ 0 };
};

//////////////////////////////////////////////////////////////////////////
// Both ends inclusive, as the drag's two pixels are both in it.
SPixelSpan ToSpan(float from, float to, int limit)
{
	int const first{ std::clamp(static_cast<int>(std::floor(std::min(from, to))), 0, limit - 1) };
	int const last{ std::clamp(static_cast<int>(std::floor(std::max(from, to))), 0, limit - 1) };

	return SPixelSpan{ first, last - first + 1 };
}

//////////////////////////////////////////////////////////////////////////
// The screenshot holds every display laid out as the desktop is, at whatever density the compositor chose.
SDL_FRect FindPrimaryInDesktop(SDL_Rect const& primary)
{
	SDL_Rect       desktop{ primary };
	int            numDisplays{ 0 };
	SDL_DisplayID* pDisplays{ SDL_GetDisplays(&numDisplays) };

	for (int index{ 0 }; pDisplays != nullptr && index < numDisplays; ++index)
	{
		SDL_Rect bounds{};

		if (SDL_GetDisplayBounds(pDisplays[index], &bounds))
		{
			SDL_GetRectUnion(&desktop, &bounds, &desktop);
		}
	}

	SDL_free(pDisplays);

	float const width{ static_cast<float>(desktop.w) };
	float const height{ static_cast<float>(desktop.h) };

	return SDL_FRect{ static_cast<float>(primary.x - desktop.x) / width,
		              static_cast<float>(primary.y - desktop.y) / height,
		              static_cast<float>(primary.w) / width, static_cast<float>(primary.h) / height };
}

//////////////////////////////////////////////////////////////////////////
SDL_Window* CreateWindowOn(SDL_DisplayID display)
{
	SDL_PropertiesID const properties{ SDL_CreateProperties() };

	SDL_SetStringProperty(properties, SDL_PROP_WINDOW_CREATE_TITLE_STRING, "Klip region");
	SDL_SetNumberProperty(properties, SDL_PROP_WINDOW_CREATE_X_NUMBER, SDL_WINDOWPOS_CENTERED_DISPLAY(display));
	SDL_SetNumberProperty(properties, SDL_PROP_WINDOW_CREATE_Y_NUMBER, SDL_WINDOWPOS_CENTERED_DISPLAY(display));
	SDL_SetNumberProperty(properties, SDL_PROP_WINDOW_CREATE_FLAGS_NUMBER, WindowFlags);

	SDL_Window* const pWindow{ SDL_CreateWindowWithProperties(properties) };

	SDL_DestroyProperties(properties);

	return pWindow;
}
} // namespace

//////////////////////////////////////////////////////////////////////////
// Takes the backdrop, which may be null.
bool CRegionSelector::Open(SDL_Surface* pBackdrop, std::string_view activationToken)
{
	SDL_DisplayID const display{ SDL_GetPrimaryDisplay() };
	SDL_Rect            bounds{};

	m_answer = ESelectorAnswer::None;
	m_isDragging = false;
	m_region = Encode::SRegion{};

	if (SDL_GetDisplayBounds(display, &bounds))
	{
		m_pWindow = CreateWindowOn(display);
		m_pRenderer = (m_pWindow != nullptr) ? SDL_CreateRenderer(m_pWindow, nullptr) : nullptr;
	}
	else
	{
		gLog.Error("Cannot read the primary display to select a region on: {}", SDL_GetError());
	}

	if (m_pRenderer != nullptr)
	{
		SDL_FRect const area{ FindPrimaryInDesktop(bounds) };

		m_uvLeft = area.x;
		m_uvTop = area.y;
		m_uvRight = area.x + area.w;
		m_uvBottom = area.y + area.h;

		if (!SDL_SetRenderVSync(m_pRenderer, 1))
		{
			gLog.Warning("Cannot enable vsync for the region selector: {}", SDL_GetError());
		}

		if (pBackdrop != nullptr)
		{
			m_pBackdrop = SDL_CreateTextureFromSurface(m_pRenderer, pBackdrop);

			if (m_pBackdrop == nullptr)
			{
				gLog.Error("Cannot show the desktop behind the region selector: {}", SDL_GetError());
			}
		}

		m_pPreviousCursor = SDL_GetCursor();
		m_pCrosshair = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_CROSSHAIR);

		if (m_pCrosshair == nullptr || !SDL_SetCursor(m_pCrosshair))
		{
			gLog.Warning("Cannot show a crosshair over the region selector: {}", SDL_GetError());
		}
	}
	else if (m_pWindow != nullptr)
	{
		gLog.Error("Cannot draw the region selector: {}", SDL_GetError());
	}
	else
	{
		gLog.Error("Cannot open the region selector: {}", SDL_GetError());
	}

	SDL_DestroySurface(pBackdrop);

	bool const isOpen{ m_pRenderer != nullptr && CreateContext() };

	if (isOpen)
	{
		// The token gives it the keyboard, which Esc needs.
		ShowHiddenWindow(m_pWindow, activationToken);
	}
	else
	{
		Close();
	}

	return isOpen;
}

//////////////////////////////////////////////////////////////////////////
void CRegionSelector::Close()
{
	if (m_pContext != nullptr)
	{
		ImGuiContext* const pPrevious{ ImGui::GetCurrentContext() };

		ImGui::SetCurrentContext(m_pContext);
		ImGui_ImplSDLRenderer3_Shutdown();
		ImGui_ImplSDL3_Shutdown();
		ImGui::DestroyContext(m_pContext);
		ImGui::SetCurrentContext(pPrevious != m_pContext ? pPrevious : nullptr);
		m_pContext = nullptr;
	}

	// The main window's backend sets a cursor only when its own choice changes, so it would never restore this.
	if (m_pCrosshair != nullptr)
	{
		SDL_SetCursor(m_pPreviousCursor);
		SDL_DestroyCursor(m_pCrosshair);
		m_pCrosshair = nullptr;
	}

	if (m_pBackdrop != nullptr)
	{
		SDL_DestroyTexture(m_pBackdrop);
		m_pBackdrop = nullptr;
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
}

//////////////////////////////////////////////////////////////////////////
bool CRegionSelector::OwnsEvent(SDL_Event const& event) const
{
	return m_pWindow != nullptr && SDL_GetWindowFromEvent(&event) == m_pWindow;
}

//////////////////////////////////////////////////////////////////////////
void CRegionSelector::ProcessEvent(SDL_Event const& event)
{
	switch (event.type)
	{
		case SDL_EVENT_MOUSE_BUTTON_DOWN:
			if (event.button.button == SDL_BUTTON_LEFT)
			{
				m_originX = event.button.x;
				m_originY = event.button.y;
				m_pointerX = event.button.x;
				m_pointerY = event.button.y;
				m_isDragging = true;
			}
			break;

		case SDL_EVENT_MOUSE_MOTION:
			m_pointerX = event.motion.x;
			m_pointerY = event.motion.y;
			break;

		case SDL_EVENT_MOUSE_BUTTON_UP:
			if (event.button.button == SDL_BUTTON_LEFT && m_isDragging)
			{
				m_isDragging = false;
				Finish(event.button.x, event.button.y);
			}
			break;

		case SDL_EVENT_KEY_DOWN:
			if (event.key.key == SDLK_ESCAPE)
			{
				m_answer = ESelectorAnswer::Cancelled;
			}
			break;

		case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
			m_answer = ESelectorAnswer::Cancelled;
			break;

		default:
			break;
	}
}

//////////////////////////////////////////////////////////////////////////
void CRegionSelector::Draw()
{
	ImGuiContext* const pPrevious{ ImGui::GetCurrentContext() };

	ImGui::SetCurrentContext(m_pContext);
	ImGui_ImplSDLRenderer3_NewFrame();
	ImGui_ImplSDL3_NewFrame();
	ImGui::NewFrame();

	ImDrawList&    drawList{ *ImGui::GetBackgroundDrawList() };
	ImGuiIO const& io{ ImGui::GetIO() };
	ImVec2 const   size{ io.DisplaySize };

	if (m_pBackdrop != nullptr)
	{
		drawList.AddImage(ImTextureRef{ static_cast<ImTextureID>(reinterpret_cast<intptr_t>(m_pBackdrop)) },
		                  ImVec2{ 0.0f, 0.0f }, size, ImVec2{ m_uvLeft, m_uvTop }, ImVec2{ m_uvRight, m_uvBottom });
	}

	if (m_isDragging)
	{
		SPixelSpan const columns{ ToSpan(m_originX, m_pointerX, static_cast<int>(size.x)) };
		SPixelSpan const rows{ ToSpan(m_originY, m_pointerY, static_cast<int>(size.y)) };
		ImVec2 const     min{ static_cast<float>(columns.first), static_cast<float>(rows.first) };
		ImVec2 const     max{ min.x + static_cast<float>(columns.count), min.y + static_cast<float>(rows.count) };

		drawList.AddRectFilled(ImVec2{ 0.0f, 0.0f }, ImVec2{ size.x, min.y }, ShadeColor);
		drawList.AddRectFilled(ImVec2{ 0.0f, max.y }, size, ShadeColor);
		drawList.AddRectFilled(ImVec2{ 0.0f, min.y }, ImVec2{ min.x, max.y }, ShadeColor);
		drawList.AddRectFilled(ImVec2{ max.x, min.y }, ImVec2{ size.x, max.y }, ShadeColor);
		drawList.AddRect(min, max, ImGui::GetColorU32(GetAccentColor()), 0.0f, BorderWidth * m_scale);

		std::array<char, 32>   text{};
		std::string_view const label{ FormatTo(text, "{} x {}", columns.count, rows.count) };

		drawList.AddText(ImVec2{ min.x + LabelMargin * m_scale, min.y + LabelMargin * m_scale }, LabelColor,
		                 label.data(), label.data() + label.size());
	}
	else
	{
		drawList.AddRectFilled(ImVec2{ 0.0f, 0.0f }, size, ShadeColor);
	}

	ImGui::Render();

	bool rendered{ SDL_SetRenderScale(m_pRenderer, io.DisplayFramebufferScale.x, io.DisplayFramebufferScale.y) };

	rendered = SDL_SetRenderDrawColor(m_pRenderer, 0, 0, 0, SDL_ALPHA_OPAQUE) && rendered;
	rendered = SDL_RenderClear(m_pRenderer) && rendered;
	ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), m_pRenderer);
	rendered = SDL_RenderPresent(m_pRenderer) && rendered;

	if (!rendered)
	{
		gLog.Error("Drawing the region selector failed: {}", SDL_GetError());
	}

	ImGui::SetCurrentContext(pPrevious);
}

//////////////////////////////////////////////////////////////////////////
// Its own, since a renderer's font atlas cannot be shared; nothing in it asks for a cursor.
bool CRegionSelector::CreateContext()
{
	ImGuiContext* const pPrevious{ ImGui::GetCurrentContext() };
	bool                isCreated{ false };

	// CreateContext leaves whichever context was current in place.
	m_pContext = ImGui::CreateContext();
	ImGui::SetCurrentContext(m_pContext);

	ImGuiIO& io{ ImGui::GetIO() };

	io.IniFilename = nullptr;
	io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;
	m_scale = ReadUiScale(m_pWindow);
	ApplyTheme(m_scale);

	if (LoadFonts() && ImGui_ImplSDL3_InitForSDLRenderer(m_pWindow, m_pRenderer))
	{
		isCreated = ImGui_ImplSDLRenderer3_Init(m_pRenderer);

		if (!isCreated)
		{
			gLog.Error("Cannot initialize ImGui's SDL renderer backend for the region selector");
			ImGui_ImplSDL3_Shutdown();
		}
	}
	else
	{
		gLog.Error("Cannot initialize ImGui for the region selector");
	}

	if (!isCreated)
	{
		ImGui::DestroyContext(m_pContext);
		m_pContext = nullptr;
	}

	ImGui::SetCurrentContext(pPrevious);

	return isCreated;
}

//////////////////////////////////////////////////////////////////////////
// Too small a drag is taken back, and the next one starts afresh.
void CRegionSelector::Finish(float x, float y)
{
	int width{ 0 };
	int height{ 0 };

	SDL_GetWindowSize(m_pWindow, &width, &height);

	SPixelSpan const columns{ ToSpan(m_originX, x, width) };
	SPixelSpan const rows{ ToSpan(m_originY, y, height) };

	if (columns.count >= MinimumSide && rows.count >= MinimumSide)
	{
		m_region = Encode::SRegion{ static_cast<uint32_t>(columns.first), static_cast<uint32_t>(rows.first),
			                        static_cast<uint32_t>(columns.count), static_cast<uint32_t>(rows.count) };
		m_answer = ESelectorAnswer::Accepted;
	}
}
} // namespace Klip
