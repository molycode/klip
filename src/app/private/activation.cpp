#include "activation.hpp"

#include "log.hpp"

#include <SDL3/SDL.h>

#include <string>

namespace Klip
{
namespace
{
constexpr char const* TokenVariable{ "XDG_ACTIVATION_TOKEN" };

//////////////////////////////////////////////////////////////////////////
bool IsHidden(SDL_Window* pWindow)
{
	return (SDL_GetWindowFlags(pWindow) & SDL_WINDOW_HIDDEN) != 0;
}

//////////////////////////////////////////////////////////////////////////
bool IsWayland()
{
	char const* const pDriver{ SDL_GetCurrentVideoDriver() };

	return pDriver != nullptr && std::string_view{ pDriver } == "wayland";
}

//////////////////////////////////////////////////////////////////////////
// SDL reads the token from its own environment copy, on a show only; cleared after, or SDL_OpenURL hands it on.
void ShowWithToken(SDL_Window* pWindow, std::string_view activationToken)
{
	SDL_Environment* const pEnvironment{ SDL_GetEnvironment() };

	if (!activationToken.empty() &&
	    !SDL_SetEnvironmentVariable(pEnvironment, TokenVariable, std::string{ activationToken }.c_str(), true))
	{
		gLog.Warning("Cannot pass the activation token on, so the window may open behind others: {}",
		             SDL_GetError());
	}

	if (!SDL_ShowWindow(pWindow))
	{
		gLog.Error("Cannot show the window: {}", SDL_GetError());
	}

	SDL_UnsetEnvironmentVariable(pEnvironment, TokenVariable);
}
} // namespace

//////////////////////////////////////////////////////////////////////////
void ShowHiddenWindow(SDL_Window* pWindow, std::string_view activationToken)
{
	if (IsHidden(pWindow))
	{
		ShowWithToken(pWindow, activationToken);
	}
}

//////////////////////////////////////////////////////////////////////////
// SDL raises a mapped Wayland window with a token GNOME refuses while Klip lacks focus, so it is hidden first.
void PresentWindow(SDL_Window* pWindow, std::string_view activationToken)
{
	if (!IsHidden(pWindow) && !activationToken.empty() && IsWayland())
	{
		SDL_HideWindow(pWindow);
	}

	if (IsHidden(pWindow))
	{
		ShowWithToken(pWindow, activationToken);
	}
	else
	{
		SDL_RestoreWindow(pWindow);
		SDL_RaiseWindow(pWindow);
	}
}
} // namespace Klip
