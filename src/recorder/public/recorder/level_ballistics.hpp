#pragma once

#include <tge/non_copyable.hpp>

#include <array>
#include <chrono>
#include <cstdint>
#include <optional>
#include <span>

namespace Klip::Recorder
{
// A peak meter's motion: a bar that falls rather than drops, a hold marker, and a latched clip.
class CLevelBallistics final : private Tge::SNoCopyNoMove
{
public:

	static constexpr uint32_t MaxChannels{ 2 };

	using TimePoint = std::chrono::steady_clock::time_point;

	CLevelBallistics() = default;
	~CLevelBallistics() = default;

	void Feed(std::span<float const> peaks, TimePoint now);
	void Reset();

	void SetUnavailable(bool unavailable) { m_unavailable = unavailable; }
	bool IsUnavailable() const { return m_unavailable; }

	float GetLevel(uint32_t channel) const { return m_channels[channel].level; }
	float GetHold(uint32_t channel) const { return m_channels[channel].hold; }
	bool  IsClipped(uint32_t channel) const { return m_channels[channel].clipped; }

private:

	struct SChannel final
	{
		float                    level{ 0.0f };
		float                    hold{ 0.0f };
		std::optional<TimePoint> heldSince;
		std::optional<TimePoint> fallingSince;
		TimePoint                clippedSince;
		bool                     clipped{ false };
	};

	std::array<SChannel, MaxChannels> m_channels;

	bool m_unavailable{ false };
};
} // namespace Klip::Recorder
