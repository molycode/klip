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

enum class EPortalProblem : uint8_t
{
	None,
	Missing,
	TooOld,
	NoMonitor
};

struct SPortalGrant final
{
	EPortalResult result{ EPortalResult::Failed };
	SStreamInfo   stream;

	int pipeWireFd{ -1 };

	std::string restoreToken;
};

bool KeepsGrant(ESourceType source, bool rememberWindow);

class CPortalSession final : private Tge::SNoCopyNoMove
{
public:

	using ResultCallback = std::function<void(SPortalGrant const&)>;

	using ClosedCallback = std::function<void()>;

	CPortalSession() = default;
	~CPortalSession() = default;

	bool Initialize(ClosedCallback onClosed);

	void Terminate();

	void Close();

	void Start(ESourceType source, bool rememberWindow, std::string restoreToken, ResultCallback callback);

	EPortalProblem GetProblem() const { return m_problem; }

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
	EPortalProblem m_problem{ EPortalProblem::None };
	uint32_t       m_availableSourceTypes{ 0 };
	uint32_t       m_availableCursorModes{ 0 };
	bool           m_rememberWindow{ false };
	bool           m_initialized{ false };
	bool           m_busy{ false };
	bool           m_sessionLive{ false };
};
} // namespace Klip::Capture
