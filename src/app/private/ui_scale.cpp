#include "ui_scale.hpp"

#include "log.hpp"

#include <SDL3/SDL.h>

namespace Klip
{
//////////////////////////////////////////////////////////////////////////
// The display scale less the pixel density, which ImGui's framebuffer scale already covers.
float ReadUiScale(SDL_Window* pWindow)
{
	float       scale{ 1.0f };
	float const displayScale{ SDL_GetWindowDisplayScale(pWindow) };
	float const pixelDensity{ SDL_GetWindowPixelDensity(pWindow) };

	if (displayScale > 0.0f && pixelDensity > 0.0f)
	{
		scale = displayScale / pixelDensity;
	}
	else
	{
		gLog.Warning("Cannot read the window's display scale, using 1.0: {}", SDL_GetError());
	}

	return scale;
}
} // namespace Klip
