#include "recorder/format.hpp"

#include <format>

namespace Klip::Recorder
{
namespace
{
constexpr std::chrono::seconds RateSettle{ 3 };

constexpr double BytesPerMebibyte{ 1024.0 * 1024.0 };
} // namespace

//////////////////////////////////////////////////////////////////////////
std::string FormatDuration(std::chrono::milliseconds elapsed)
{
	int64_t const totalSeconds{ std::chrono::duration_cast<std::chrono::seconds>(elapsed).count() };

	return std::format("{:02}:{:02}:{:02}", totalSeconds / 3600, (totalSeconds / 60) % 60, totalSeconds % 60);
}

//////////////////////////////////////////////////////////////////////////
std::string FormatBytes(uint64_t bytes)
{
	return std::format("{:.1f} MiB", static_cast<double>(bytes) / BytesPerMebibyte);
}

//////////////////////////////////////////////////////////////////////////
SThroughput FormatThroughput(uint64_t bytes, std::chrono::seconds elapsed)
{
	SThroughput result;

	if (elapsed >= RateSettle)
	{
		double const perMinute{ static_cast<double>(bytes) / BytesPerMebibyte * 60.0 /
		                        static_cast<double>(elapsed.count()) };
		double const perHour{ perMinute * 60.0 };

		result.perMinute = std::format("{:.1f} MiB/min", perMinute);
		result.perHour   = perHour >= 1024.0 ? std::format("{:.1f} GiB/h", perHour / 1024.0)
		                                     : std::format("{:.0f} MiB/h", perHour);
	}

	return result;
}
} // namespace Klip::Recorder
