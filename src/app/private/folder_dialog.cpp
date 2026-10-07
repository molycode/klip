#include "folder_dialog.hpp"

#include "folder_dialog_mailbox.hpp"

#include <SDL3/SDL.h>
#include <tge/assert.hpp>

#include <string_view>

namespace Klip
{
namespace
{
// Static and trivially destructible: zenity calls back from a thread of its own, even after Klip has shut down.
constinit SFolderDialogMailbox gMailbox{};

//////////////////////////////////////////////////////////////////////////
// After shutdown neither the logger, the allocator nor SDL's event queue may be used.
void Finish(EFolderDialogState state, std::string_view text)
{
	gMailbox.textLength = text.copy(gMailbox.text.data(), gMailbox.text.size());
	gMailbox.state.store(state, std::memory_order_release);
}

//////////////////////////////////////////////////////////////////////////
// zenity reports a cancel as one empty path.
void SDLCALL OnDialogFinished(void*, char const* const* pFileList, int)
{
	if (pFileList == nullptr)
	{
		std::string_view const error{ SDL_GetError() };

		Finish(EFolderDialogState::Failed, error.empty() ? std::string_view{ "SDL gave no reason" } : error);
	}
	else if (pFileList[0] == nullptr || pFileList[0][0] == '\0')
	{
		Finish(EFolderDialogState::Cancelled, {});
	}
	else
	{
		std::string_view const path{ pFileList[0] };

		if (path.size() <= gMailbox.text.size())
		{
			Finish(EFolderDialogState::Picked, path);
		}
		else
		{
			Finish(EFolderDialogState::Failed, "the chosen path is too long");
		}
	}
}
} // namespace

//////////////////////////////////////////////////////////////////////////
// Open is stored first: SDL calls back before returning when it cannot show a dialog.
void OpenFolderDialog(SDL_Window* pWindow, char const* pStartFolder)
{
	TGE_ASSERT(gMailbox.state.load(std::memory_order_acquire) == EFolderDialogState::Closed,
	           "A folder dialog opens over one whose answer was never taken");

	gMailbox.state.store(EFolderDialogState::Open, std::memory_order_relaxed);
	SDL_ShowOpenFolderDialog(&OnDialogFinished, nullptr, pWindow, pStartFolder, false);
}

//////////////////////////////////////////////////////////////////////////
bool IsFolderDialogPending()
{
	return gMailbox.state.load(std::memory_order_acquire) != EFolderDialogState::Closed;
}

//////////////////////////////////////////////////////////////////////////
std::optional<SFolderDialogResult> TakeFolderDialogResult()
{
	std::optional<SFolderDialogResult> result{};
	EFolderDialogState const           state{ gMailbox.state.load(std::memory_order_acquire) };

	if (state != EFolderDialogState::Closed && state != EFolderDialogState::Open)
	{
		result = SFolderDialogResult{ state, std::string{ gMailbox.text.data(), gMailbox.textLength } };
		gMailbox.state.store(EFolderDialogState::Closed, std::memory_order_relaxed);
	}

	return result;
}
} // namespace Klip
