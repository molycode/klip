#pragma once

#include "folder_dialog_state.hpp"

#include <array>
#include <atomic>
#include <cstddef>

namespace Klip
{
struct SFolderDialogMailbox final
{
	std::atomic<EFolderDialogState> state{ EFolderDialogState::Closed };
	std::array<char, 4096>          text{};
	size_t                          textLength{ 0 };
};
} // namespace Klip
