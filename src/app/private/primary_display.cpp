#include "primary_display.hpp"

#include "log.hpp"

#include <SDL3/SDL.h>

#include <cmath>
#include <cstdint>

namespace Klip
{
//////////////////////////////////////////////////////////////////////////
// In logical pixels, as the portal streams it.
Recorder::SScreen GetPrimaryScreen()
{
	Recorder::SScreen          screen{};
	SDL_DisplayMode const* const pMode{ SDL_GetCurrentDisplayMode(SDL_GetPrimaryDisplay()) };

	if (pMode != nullptr)
	{
		screen = Recorder::SScreen{ static_cast<uint32_t>(pMode->w), static_cast<uint32_t>(pMode->h),
			                        static_cast<uint32_t>(std::lround(pMode->refresh_rate)) };
	}
	else
	{
		gLog.Warning("Cannot read the primary display, so the size estimate has no screen to go on: {}",
		             SDL_GetError());
	}

	return screen;
}
} // namespace Klip
