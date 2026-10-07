#pragma once

#include "capture/audio_device.hpp"

#include <tge/non_copyable.hpp>

#include <vector>

namespace Klip::Capture
{
class CDevicesImpl;

class CAudioDevices final : private Tge::SNoCopyNoMove
{
public:

	CAudioDevices() = default;
	~CAudioDevices() = default;

	bool Initialize();
	void Terminate();

	void Refresh();

	std::vector<SAudioDevice> const& GetSinks() const;

	std::vector<SAudioDevice> const& GetSources() const;

private:

	CDevicesImpl* m_pImpl{ nullptr };

	std::vector<SAudioDevice> m_sinks;
	std::vector<SAudioDevice> m_sources;
};
} // namespace Klip::Capture
