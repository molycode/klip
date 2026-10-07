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

	using BufferCallback = std::function<void(SAudioBuffer const&)>;

	using EndedCallback = std::function<void()>;

	CAudioStream() = default;
	~CAudioStream() = default;

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
