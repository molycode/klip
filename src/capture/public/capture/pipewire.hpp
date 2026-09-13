#pragma once

namespace Klip::Capture
{
// PipeWire is one library for the whole process, initialised by whoever owns the streams rather than by
// each stream: otherwise one stream's teardown deinitialises it under another that is still running.
void InitializePipeWire();
void TerminatePipeWire();
} // namespace Klip::Capture
