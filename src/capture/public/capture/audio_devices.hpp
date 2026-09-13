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

	// Synchronous: it round-trips the server, so the lists are current when it returns.
	void Refresh();

	std::vector<SAudioDevice> const& GetSinks() const;

	// Every input. The server's own default source is deliberately not consulted: it was measured
	// pointing at a sink, which would silently record system audio through a microphone toggle.
	std::vector<SAudioDevice> const& GetSources() const;

private:

	CDevicesImpl* m_pImpl{ nullptr };

	std::vector<SAudioDevice> m_sinks;
	std::vector<SAudioDevice> m_sources;
};
} // namespace Klip::Capture
