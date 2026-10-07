#pragma once

namespace Klip::Recorder
{
struct SSettingsChanges final
{
	// Any of the user's choices; they are written together, as one set.
	bool choices{ false };
	bool screenToken{ false };
	bool windowToken{ false };
};
} // namespace Klip::Recorder
