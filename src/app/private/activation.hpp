#pragma once

#include <string_view>

struct SDL_Window;

namespace Klip
{
void ShowHiddenWindow(SDL_Window* pWindow, std::string_view activationToken);
void PresentWindow(SDL_Window* pWindow, std::string_view activationToken);
} // namespace Klip
