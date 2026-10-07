#pragma once

#include <tge/non_copyable.hpp>

#include <cstdint>
#include <functional>
#include <string>

struct sd_bus;
struct sd_bus_message;
struct sd_bus_slot;

namespace Klip::Capture
{
struct SStreamInfo final
{
	uint32_t nodeId{ 0 };

	// Logical layout size. Mutter streams at this size too, so a scaled display records scaled.
	uint32_t width{ 0 };
	uint32_t height{ 0 };
};

enum class ESourceType : uint8_t
{
	Screen,
	Window
};

enum class EPortalResult : uint8_t
{
	Success,
	Cancelled,
	Failed
};

struct SPortalGrant final
{
	EPortalResult result{ EPortalResult::Failed };
	SStreamInfo   stream;

	// On success the receiver owns it and must close it.
	int pipeWireFd{ -1 };

	// A token is spent by the request that carries it, so this replaces it even when the recording then
	// fails to start.
	std::string restoreToken;
};

// A screen is the same screen next time, so its grant is always worth keeping. Which window someone wants
// is a fresh question unless they say otherwise.
bool KeepsGrant(ESourceType source, bool rememberWindow);

// Runs on Bus::gConnection's thread, where its callbacks fire too.
class CPortalSession final : private Tge::SNoCopyNoMove
{
public:

	using ResultCallback = std::function<void(SPortalGrant const&)>;

	using ClosedCallback = std::function<void()>;

	CPortalSession() = default;
	~CPortalSession() = default;

	bool Initialize(ClosedCallback onClosed);

	// No callback fires once this returns.
	void Terminate();

	// Idempotent; a later Start opens a fresh session.
	void Close();

	void Start(ESourceType source, bool rememberWindow, std::string restoreToken, ResultCallback callback);

private:

	enum class EStep : uint8_t
	{
		None,
		CreateSession,
		SelectSources,
		Start
	};

	void Probe(sd_bus* pBus);
	void Begin(sd_bus* pBus, ResultCallback callback);

	// Subscribes before the call goes out; the portal can answer first.
	bool Request(sd_bus* pBus, EStep step, char const* pMember,
	             std::function<int(sd_bus_message*, std::string const& token)> const& fill);

	void OnResponse(sd_bus_message* pMessage);
	void OnSessionCreated(sd_bus* pBus, sd_bus_message* pResults);
	void OnSourcesSelected(sd_bus* pBus);
	void OnStarted(sd_bus* pBus, sd_bus_message* pResults);
	void OnSessionClosed();

	void OpenRemote(sd_bus* pBus, SPortalGrant grant);
	void CloseSession(sd_bus* pBus);
	void Finish(sd_bus* pBus, SPortalGrant const& grant);

	ResultCallback m_callback;
	ClosedCallback m_onClosed;
	std::string    m_sessionHandle;
	std::string    m_restoreToken;
	sd_bus_slot*   m_pResponseSlot{ nullptr };
	sd_bus_slot*   m_pClosedSlot{ nullptr };
	EStep          m_step{ EStep::None };
	ESourceType    m_source{ ESourceType::Screen };
	uint32_t       m_availableSourceTypes{ 0 };
	uint32_t       m_availableCursorModes{ 0 };
	bool           m_rememberWindow{ false };
	bool           m_initialized{ false };
	bool           m_busy{ false };
	bool           m_sessionLive{ false };
};
} // namespace Klip::Capture
