#include "capture/pipewire.hpp"

#include "audio_loop.hpp"

#include <pipewire/pipewire.h>

namespace Klip::Capture
{
//////////////////////////////////////////////////////////////////////////
// Once for the process, by whoever owns the streams: one stream's teardown would otherwise deinitialise it
// under another still running.
void InitializePipeWire()
{
	pw_init(nullptr, nullptr);
}

//////////////////////////////////////////////////////////////////////////
void TerminatePipeWire()
{
	// Before pw_deinit, which pulls the library out from under anything still holding a loop.
	gAudioLoop.Terminate();

	pw_deinit();
}
} // namespace Klip::Capture
