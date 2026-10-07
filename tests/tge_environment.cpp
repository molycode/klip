#include "tge_environment.hpp"

#include <tge/init/init.hpp>
#include <tge/logging/log_system.hpp>

namespace Klip::Tests
{
//////////////////////////////////////////////////////////////////////////
void CTgeEnvironment::SetUp()
{
	Tge::Logging::CLogSystem& logSystem{ Tge::Logging::GetLogSystem() };

	// Running, so a CExpectedLog's listener sees messages; terminal only, so nothing else queues for listeners.
	logSystem.Initialize("klip-tests", "", "");
	logSystem.SetEnabledTargets(Tge::Logging::ETarget::Terminal);

	Tge::Initialize(0);
}

//////////////////////////////////////////////////////////////////////////
void CTgeEnvironment::TearDown()
{
	Tge::Terminate();
	Tge::Logging::GetLogSystem().Terminate();
}
} // namespace Klip::Tests
