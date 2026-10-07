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

	using FrameCallback = std::function<void(SFrame const&)>;

	using EndedCallback = std::function<void()>;

	CPipeWireStream() = default;
	~CPipeWireStream() = default;

	bool Initialize(int pipeWireFd, uint32_t nodeId, uint32_t maxFrameRate, FrameCallback callback,
	                EndedCallback onEnded);
	void Terminate();

	uint32_t GetWidth() const;
	uint32_t GetHeight() const;

	uint32_t GetMaxFrameRate() const;
	uint64_t GetNumFrames() const;

private:

	CStreamImpl* m_pImpl{ nullptr };
	uint64_t     m_numFrames{ 0 };
};
} // namespace Klip::Capture
