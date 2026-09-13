#if !defined(KLIP_PLATFORM_LINUX) && !defined(KLIP_PLATFORM_WINDOWS)
#error "No KLIP_PLATFORM_* define. cmake/platform.cmake did not run."
#endif // platform define present

#include "log.hpp"
#include "main_window.hpp"
#include "single_instance.hpp"

#include <capture/pipewire.hpp>
#include <encode/capabilities.hpp>

#include <tge/init/init.hpp>
#include <tge/logging/log_system.hpp>
#include <tge/profiling/profiler_hooks.hpp>

#include <QtWidgets/QApplication>

#include <cstdio>
#include <string_view>

namespace
{
bool WantsVersion(int argc, char** argv)
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
} // namespace

//////////////////////////////////////////////////////////////////////////
int main(int argc, char** argv)
{
	// Answered before the log system and Qt, so it works over ssh and on a machine with no display.
	if (WantsVersion(argc, argv))
	{
		std::puts("Klip " KLIP_VERSION);

		return 0;
	}

	Tge::Logging::GetLogSystem().Initialize("klip");

	// Before Tge::Initialize, where the job pool spawns: a thread started after the hooks are in place
	// is a thread the profiler can name.
	Tge::Profiling::RegisterHooks();

	QApplication app(argc, argv);
	QApplication::setStyle(QStringLiteral("Fusion"));
	QApplication::setOrganizationName(QStringLiteral("klip"));
	QApplication::setApplicationName(QStringLiteral("Klip"));
	QApplication::setDesktopFileName(QStringLiteral("klip"));

	// Hiding to the tray while recording must not be read as the last window closing.
	QApplication::setQuitOnLastWindowClosed(false);

	int exitCode{ 1 };

	Klip::CSingleInstance instance;

	if (!instance.Claim())
	{
		Klip::gLog.Info("Klip is already running; asked it to show itself");
		Tge::Logging::GetLogSystem().Terminate();

		return 0;
	}

	// Klip queues no jobs, and the default sizes the pool to hardware_concurrency.
	bool initialized{ false };

	{
		TGE_PROFILE_SCOPE_N("Startup: tge-core");
		initialized = Tge::Initialize(0);
	}

	if (initialized)
	{
		Klip::gLog.Info("Klip {} started", KLIP_VERSION);

		// Before anything makes a PipeWire object, which both the capture streams and the device
		// listing do.
		{
			TGE_PROFILE_SCOPE_N("Startup: pipewire");
			Klip::Capture::InitializePipeWire();
		}

		// Before the window: it lists only the codecs this card answered for.
		{
			TGE_PROFILE_SCOPE_N("Startup: encoder probe");
			Klip::Encode::InitializeCapabilities();
		}

		Klip::CMainWindow window;

		bool opened{ false };

		{
			TGE_PROFILE_SCOPE_N("Startup: window");
			opened = window.Initialize();
		}

		if (opened)
		{
			QObject::connect(&instance, &Klip::CSingleInstance::ShowRequested, &window,
			                 &Klip::CMainWindow::Reveal);

			window.show();
			exitCode = QApplication::exec();
		}

		window.Terminate();
		Klip::Capture::TerminatePipeWire();
		instance.Terminate();

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
