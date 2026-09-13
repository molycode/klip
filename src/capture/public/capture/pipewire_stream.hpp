#pragma once

#include "capture/frame.hpp"

#include <tge/non_copyable.hpp>

#include <cstdint>
#include <functional>

namespace Klip::Capture
{
class CStreamImpl;

class CPipeWireStream final : private Tge::SNoCopyNoMove
{
public:

	// Fires on the PipeWire thread. The pixels die when it returns, so copy anything you keep.
	using FrameCallback = std::function<void(SFrame const&)>;

	// Also on the PipeWire thread, and at most once.
	using EndedCallback = std::function<void()>;

	CPipeWireStream() = default;
	~CPipeWireStream() = default;

	// Takes ownership of pipeWireFd. maxFrameRate is a ceiling asked of the compositor, zero for none;
	// a screen that changes less often still delivers less than it.
	bool Initialize(int pipeWireFd, uint32_t nodeId, uint32_t maxFrameRate, FrameCallback callback,
	                EndedCallback onEnded);
	void Terminate();

	uint32_t GetWidth() const;
	uint32_t GetHeight() const;

	// The ceiling the compositor settled on, which is the display's refresh rate when none was asked for.
	uint32_t GetMaxFrameRate() const;
	uint64_t GetNumFrames() const;

private:

	CStreamImpl* m_pImpl{ nullptr };
	uint64_t     m_numFrames{ 0 };
};
} // namespace Klip::Capture
