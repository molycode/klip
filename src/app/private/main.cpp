#if !defined(KLIP_PLATFORM_LINUX) && !defined(KLIP_PLATFORM_WINDOWS)
#error "No KLIP_PLATFORM_* define. cmake/platform.cmake did not run."
#endif // platform define present

#include "application.hpp"
#include "bus/connection.hpp"
#include "desktop/single_instance.hpp"
#include "log.hpp"

#include <capture/pipewire.hpp>
#include <encode/capabilities.hpp>

#include <tge/init/init.hpp>
#include <tge/logging/log_system.hpp>
#include <tge/profiling/profiler_hooks.hpp>

#include <cstdio>
#include <cstdlib>
#include <string_view>

namespace
{
bool WantsVersion(int argc, char const* const* argv)
{
	bool wanted{ false };

	for (int index{ 1 }; index < argc; ++index)
	{
		if (std::string_view{ argv[index] } == "--version")
		{
			wanted = true;
		}
	}

	return wanted;
}

// The first Klip of the session, from PipeWire to the end of the event loop.
int RunKlip()
{
	int exitCode{ 1 };

	// Before anything makes a PipeWire object, which both the capture streams and the device listing do.
	{
		TGE_PROFILE_SCOPE_N("Startup: pipewire");
		Klip::Capture::InitializePipeWire();
	}

	// Before the window: it lists only the codecs this card answered for.
	{
		TGE_PROFILE_SCOPE_N("Startup: encoder probe");
		Klip::Encode::InitializeCapabilities();
	}

	Klip::CApplication application;

	bool opened{ false };

	{
		TGE_PROFILE_SCOPE_N("Startup: window");
		opened = application.Initialize();
	}

	if (opened)
	{
		Klip::Desktop::gSingleInstance.Serve(
			[&application](Klip::Desktop::SRequest const& request) { application.Request(request); });

		application.Run();
		exitCode = 0;
	}

	// First: its callback points at the application.
	Klip::Desktop::gSingleInstance.Terminate();

	application.Terminate();
	Klip::Capture::TerminatePipeWire();

	return exitCode;
}
} // namespace

//////////////////////////////////////////////////////////////////////////
// NOLINTNEXTLINE(misc-const-correctness): the standard fixes main's signature.
int main(int argc, char** argv)
{
	// Answered before the log system and SDL, so it works over ssh and on a machine with no display.
	if (WantsVersion(argc, argv))
	{
		std::puts("Klip " KLIP_VERSION);

		return 0;
	}

	Tge::Logging::GetLogSystem().Initialize("klip");

	// Before Tge::Initialize, where the job pool spawns: a thread started after the hooks are in place
	// is a thread the profiler can name.
	Tge::Profiling::RegisterHooks();

	int exitCode{ 1 };

	// Klip queues no jobs, and the default sizes the pool to hardware_concurrency.
	bool initialized{ false };

	{
		TGE_PROFILE_SCOPE_N("Startup: tge-core");
		initialized = Tge::Initialize(0);
	}

	if (initialized)
	{
		bool connected{ false };

		// After tge-core, whose thread it runs on; every portal call, the tray and single instance go through it.
		{
			TGE_PROFILE_SCOPE_N("Startup: session bus");
			connected = Klip::Bus::gConnection.Initialize("klip-bus");
		}

		if (connected && Klip::Desktop::gSingleInstance.Claim())
		{
			Klip::gLog.Info("Klip {} started", KLIP_VERSION);
			exitCode = RunKlip();
		}
		else if (connected)
		{
			char const* const pToken{ std::getenv("XDG_ACTIVATION_TOKEN") };

			Klip::Desktop::gSingleInstance.AskOwnerToShow(pToken != nullptr ? pToken : "");
			Klip::gLog.Info("Klip is already running; asked it to show itself");
			exitCode = 0;
		}

		Klip::Bus::gConnection.Terminate();

		// Before the client is torn down at exit; a late static-dtor free then finds a null hook.
		Tge::Profiling::UnregisterHooks();

		Tge::Terminate();
	}
	else
	{
		Klip::gLog.Error("tge-core failed to initialize");
	}

	Tge::Logging::GetLogSystem().Terminate();

	return exitCode;
}
