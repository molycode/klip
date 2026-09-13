#pragma once

#include "capture/audio_buffer.hpp"
#include "capture/audio_device.hpp"

#include <tge/non_copyable.hpp>

#include <cstdint>
#include <functional>

namespace Klip::Capture
{
class CAudioImpl;

class CAudioStream final : private Tge::SNoCopyNoMove
{
public:

	// Fires on the audio thread, once per buffer.
	using BufferCallback = std::function<void(SAudioBuffer const&)>;

	// At most once, on the same thread.
	using EndedCallback = std::function<void()>;

	CAudioStream() = default;
	~CAudioStream() = default;

	// Returns only once the server has answered with a format: a connected stream whose device never
	// arrives waits forever, and the muxer needs the answer before it writes its header.
	bool Initialize(SAudioDevice const& device, BufferCallback callback, EndedCallback onEnded);
	void Terminate();

	uint32_t GetNumChannels() const;
	uint64_t GetNumBuffers() const;

	float GetChannelPeak(uint32_t channel) const;

private:

	CAudioImpl* m_pImpl{ nullptr };
	uint64_t    m_numBuffers{ 0 };
};
} // namespace Klip::Capture
