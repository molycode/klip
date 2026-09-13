#include "audio_loop.hpp"

#include "log.hpp"

#include <pipewire/pipewire.h>

namespace Klip::Capture
{
CAudioLoop gAudioLoop;

//////////////////////////////////////////////////////////////////////////
pw_thread_loop* CAudioLoop::Open()
{
	if (m_pLoop == nullptr)
	{
		m_pLoop = pw_thread_loop_new("klip-audio", nullptr);

		if (m_pLoop == nullptr)
		{
			gLog.Error("Could not create the audio loop.");
		}
		else if (pw_thread_loop_start(m_pLoop) < 0)
		{
			gLog.Error("Could not start the audio loop.");
			Terminate();
		}
		else
		{
			pw_thread_loop_lock(m_pLoop);

			m_pContext = pw_context_new(pw_thread_loop_get_loop(m_pLoop), nullptr, 0);

			if (m_pContext == nullptr)
			{
				gLog.Error("Could not create the audio context.");
			}
			else
			{
				m_pCore = pw_context_connect(m_pContext, nullptr, 0);

				if (m_pCore == nullptr)
				{
					gLog.Error("Could not connect to PipeWire for audio.");
				}
			}

			pw_thread_loop_unlock(m_pLoop);

			if (m_pCore == nullptr)
			{
				Terminate();
			}
		}
	}

	return m_pLoop;
}

//////////////////////////////////////////////////////////////////////////
void CAudioLoop::Terminate()
{
	if (m_pLoop != nullptr)
	{
		pw_thread_loop_stop(m_pLoop);
	}

	if (m_pCore != nullptr)
	{
		pw_core_disconnect(m_pCore);
		m_pCore = nullptr;
	}

	if (m_pContext != nullptr)
	{
		pw_context_destroy(m_pContext);
		m_pContext = nullptr;
	}

	if (m_pLoop != nullptr)
	{
		pw_thread_loop_destroy(m_pLoop);
		m_pLoop = nullptr;
	}
}
} // namespace Klip::Capture
