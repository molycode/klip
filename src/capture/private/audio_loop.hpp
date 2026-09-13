#pragma once

#include <tge/non_copyable.hpp>

struct pw_context;
struct pw_core;
struct pw_thread_loop;

namespace Klip::Capture
{
class CAudioLoop final : private Tge::SNoCopyNoMove
{
public:

	CAudioLoop() = default;
	~CAudioLoop() = default;

	pw_thread_loop* Open();
	void            Terminate();

	pw_core* GetCore() const { return m_pCore; }

private:

	pw_thread_loop* m_pLoop{ nullptr };
	pw_context*     m_pContext{ nullptr };
	pw_core*        m_pCore{ nullptr };
};

// Never closed between streams: a count would reach zero between the monitors stopping and the recording
// streams opening, which is the churn itself.
extern CAudioLoop gAudioLoop;
} // namespace Klip::Capture
