#pragma once

#include "folder_dialog_result.hpp"

#include <optional>

struct SDL_Window;

namespace Klip
{
void OpenFolderDialog(SDL_Window* pWindow, char const* pStartFolder);
bool IsFolderDialogPending();
std::optional<SFolderDialogResult> TakeFolderDialogResult();
} // namespace Klip
