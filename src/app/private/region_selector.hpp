#pragma once

#include "encode/settings.hpp"
#include "selector_answer.hpp"

#include <tge/non_copyable.hpp>

#include <string_view>

struct ImGuiContext;
struct SDL_Cursor;
struct SDL_Renderer;
struct SDL_Surface;
struct SDL_Texture;
struct SDL_Window;

union SDL_Event;

namespace Klip
{
class CRegionSelector final : private Tge::SNoCopyNoMove
{
public:

	CRegionSelector() = default;
	~CRegionSelector() = default;

	bool Open(SDL_Surface* pBackdrop, std::string_view activationToken);
	void Close();

	bool IsOpen() const { return m_pWindow != nullptr; }
	bool OwnsEvent(SDL_Event const& event) const;

	void ProcessEvent(SDL_Event const& event);
	void Draw();

	ESelectorAnswer GetAnswer() const { return m_answer; }
	Encode::SRegion const& GetRegion() const { return m_region; }

private:

	bool CreateContext();
	void Finish(float x, float y);

	SDL_Window*   m_pWindow{ nullptr };
	SDL_Renderer* m_pRenderer{ nullptr };
	SDL_Texture*  m_pBackdrop{ nullptr };
	SDL_Cursor*   m_pCrosshair{ nullptr };
	SDL_Cursor*   m_pPreviousCursor{ nullptr };
	ImGuiContext* m_pContext{ nullptr };

	float m_scale{ 1.0f };

	float m_uvLeft{ 0.0f };
	float m_uvTop{ 0.0f };
	float m_uvRight{ 1.0f };
	float m_uvBottom{ 1.0f };

	float m_originX{ 0.0f };
	float m_originY{ 0.0f };
	float m_pointerX{ 0.0f };
	float m_pointerY{ 0.0f };
	bool  m_isDragging{ false };

	Encode::SRegion m_region;
	ESelectorAnswer m_answer{ ESelectorAnswer::None };
};
} // namespace Klip
