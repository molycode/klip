#pragma once

#include "folder_dialog_state.hpp"

#include <string>

namespace Klip
{
struct SFolderDialogResult final
{
	EFolderDialogState state{ EFolderDialogState::Closed };
	std::string        text;
};
} // namespace Klip
