#if !defined(KLIP_PLATFORM_LINUX) && !defined(KLIP_PLATFORM_WINDOWS)
#error "No KLIP_PLATFORM_* define. cmake/platform.cmake did not run."
#endif // platform define present

#include "application.hpp"
#include "bus/connection.hpp"
#include "config/xdg_paths.hpp"
#include "desktop/single_instance.hpp"
#include "log.hpp"

#include <capture/pipewire.hpp>
#include <encode/capabilities.hpp>

#include <tge/init/init.hpp>
#include <tge/logging/log_system.hpp>
#include <tge/profiling/profiler_hooks.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace
{
constexpr size_t           MaxLogFiles{ 10 };
constexpr std::string_view LogFilePrefix{ "klip_" };
constexpr std::string_view LogFileExtension{ ".log" };

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

// Created here because the log system's own create_directories is the throwing overload.
std::filesystem::path MakeLogsDirectory(std::error_code& error)
{
	std::filesystem::path const stateHome{ Klip::Config::GetStateHome() };
	std::filesystem::path       logsDir{};

	if (!stateHome.empty())
	{
		logsDir = stateHome / "klip" / "logs";
		std::filesystem::create_directories(logsDir, error);
	}

	return logsDir;
}

void ReportLogsDirectory(std::filesystem::path const& logsDir, std::error_code const& error)
{
	if (logsDir.empty())
	{
		Klip::gLog.Warning("Neither XDG_STATE_HOME nor HOME is an absolute path; logging to the terminal only");
	}
	else if (error.value() != 0)
	{
		Klip::gLog.Warning("Cannot create the log directory '{}', logging to the terminal only: {}", logsDir.string(),
		                   error.message());
	}
}

// Names carry the start time, so sorting them sorts the logs by age.
void PruneLogs(std::filesystem::path const& logsDir)
{
	std::vector<std::filesystem::path> logFiles{};
	std::error_code                    error{};

	for (std::filesystem::directory_iterator it{ logsDir, error }, end{}; error.value() == 0 && it != end;
	     it.increment(error))
	{
		std::string const name{ it->path().filename().string() };

		if (name.starts_with(LogFilePrefix) && name.ends_with(LogFileExtension))
		{
			logFiles.emplace_back(it->path());
		}
	}

	if (error.value() == 0)
	{
		std::ranges::sort(logFiles, std::ranges::greater{});

		for (size_t index{ MaxLogFiles }; index < logFiles.size(); ++index)
		{
			std::error_code removeError{};

			std::filesystem::remove(logFiles[index], removeError);

			if (removeError.value() != 0)
			{
				Klip::gLog.Warning("Cannot remove the old log file '{}': {}", logFiles[index].string(),
				                   removeError.message());
			}
		}
	}
	else
	{
		Klip::gLog.Warning("Cannot list the log directory '{}' to remove old logs: {}", logsDir.string(),
		                   error.message());
	}
}

// The first Klip of the session, from PipeWire to the end of the event loop.
int RunKlip(std::filesystem::path const& logsDir)
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
		opened = application.Initialize(logsDir);
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

	std::error_code             logsError{};
	std::filesystem::path const logsDir{ MakeLogsDirectory(logsError) };
	bool const                  hasLogsDir{ !logsDir.empty() && logsError.value() == 0 };

	Tge::Logging::GetLogSystem().Initialize("klip", hasLogsDir ? logsDir.string() : std::string{});

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
		ReportLogsDirectory(logsDir, logsError);

		if (hasLogsDir)
		{
			PruneLogs(logsDir);
		}

		bool connected{ false };

		// After tge-core, whose thread it runs on; every portal call, the tray and single instance go through it.
		{
			TGE_PROFILE_SCOPE_N("Startup: session bus");
			connected = Klip::Bus::gConnection.Initialize("klip-bus");
		}

		if (connected && Klip::Desktop::gSingleInstance.Claim())
		{
			Klip::gLog.Info("Klip {} started", KLIP_VERSION);
			exitCode = RunKlip(hasLogsDir ? logsDir : std::filesystem::path{});
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
