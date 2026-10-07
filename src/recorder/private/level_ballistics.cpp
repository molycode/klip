#include "recorder/level_ballistics.hpp"

#include "recorder/decibels.hpp"

#include <algorithm>

namespace Klip::Recorder
{
namespace
{
constexpr std::chrono::milliseconds Hold{ 1500 };
constexpr float                     HoldFallPerSecond{ 20.0f };
constexpr float                     LevelFallPerSecond{ 60.0f };

constexpr std::chrono::milliseconds ClipLatch{ 3000 };

//////////////////////////////////////////////////////////////////////////
float ToSeconds(std::chrono::steady_clock::duration duration)
{
	return std::chrono::duration<float>{ duration }.count();
}
} // namespace

//////////////////////////////////////////////////////////////////////////
void CLevelBallistics::Feed(std::span<float const> peaks, TimePoint now)
{
	size_t const numChannels{ std::min(peaks.size(), m_channels.size()) };

	for (size_t index{ 0 }; index < numChannels; ++index)
	{
		SChannel&   channel{ m_channels[index] };
		float const peak{ peaks[index] };

		// Buffers arrive in bursts, so a bar driven straight off the last one flickers and reads low.
		float const elapsed{ channel.fallingSince.has_value() ? ToSeconds(now - *channel.fallingSince) : 0.0f };
		float const fallen{ FromDecibels(ToDecibels(channel.level) - LevelFallPerSecond * elapsed) };

		channel.level = std::max(peak, fallen);
		channel.fallingSince = now;

		if (peak >= 1.0f)
		{
			channel.clipped = true;
			channel.clippedSince = now;
		}
		else if (channel.clipped && now - channel.clippedSince > ClipLatch)
		{
			channel.clipped = false;
		}

		if (peak >= channel.hold || !channel.heldSince.has_value())
		{
			channel.hold = peak;
			channel.heldSince = now;
		}
		else if (now - *channel.heldSince > Hold)
		{
			float const holding{ ToSeconds(now - *channel.heldSince - Hold) };

			channel.hold = std::max(peak, FromDecibels(ToDecibels(channel.hold) - HoldFallPerSecond * holding));
		}
	}
}

//////////////////////////////////////////////////////////////////////////
void CLevelBallistics::Reset()
{
	for (SChannel& channel : m_channels)
	{
		channel = SChannel{};
	}
}
} // namespace Klip::Recorder
