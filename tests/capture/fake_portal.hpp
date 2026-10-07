#pragma once

#include "bus/connection.hpp"
#include "bus/portal.hpp"

#include <systemd/sd-bus.h>
#include <tge/non_copyable.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace Klip::Tests
{
struct SFakeStream final
{
	uint32_t nodeId{ 0 };
	int32_t  width{ 0 };
	int32_t  height{ 0 };
	bool     hasSize{ true };
};

struct SFakeScript final
{
	uint32_t                 version{ 5 };
	uint32_t                 createSessionResponse{ 0 };
	uint32_t                 selectSourcesResponse{ 0 };
	uint32_t                 startResponse{ 0 };
	std::string              restoreToken{ "granted" };
	std::vector<SFakeStream> streams{ { .nodeId = 42, .width = 2880, .height = 1620, .hasSize = true } };

	bool holdStart{ false };

	uint32_t    screenshotResponse{ 0 };
	std::string screenshotUri{ "file:///tmp/klip-test-shot.png" };
};

struct SFakeRecord final
{
	uint32_t    types{ 0 };
	uint32_t    persistMode{ 0 };
	std::string restoreToken;
	uint32_t    numCloses{ 0 };
	int         remotePeer{ -1 };
	bool        screenshotInteractive{ true };
};

class CFakePortal final : private Tge::SNoCopyNoMove
{
public:

	CFakePortal() = default;
	~CFakePortal() = default;

	bool Initialize();
	void Terminate();

	void        Configure(SFakeScript const& script);
	SFakeRecord GetRecord();

	void ReleaseStart();
	void CloseFromCompositor();

	void Withdraw();
	bool Restore();

	int GetVersion(sd_bus_message* pReply);
	int OnCreateSession(sd_bus_message* pMessage);
	int OnSelectSources(sd_bus_message* pMessage);
	int OnStart(sd_bus_message* pMessage);
	int OnOpenPipeWireRemote(sd_bus_message* pMessage);
	int OnClose(sd_bus_message* pMessage);
	int OnScreenshot(sd_bus_message* pMessage);

private:

	int Answer(sd_bus_message* pCall, std::string const& token, uint32_t response,
	           std::vector<Bus::SOption> const& results);
	int SendStartResponse(sd_bus* pBus);

	Bus::CConnection m_connection;
	SFakeScript      m_script;
	SFakeRecord      m_record;
	std::string      m_sessionPath;
	std::string      m_heldRequestPath;
	sd_bus_slot*     m_pScreenCastSlot{ nullptr };
	sd_bus_slot*     m_pSessionSlot{ nullptr };
	sd_bus_slot*     m_pScreenshotSlot{ nullptr };
	bool             m_releaseOnArrival{ false };
};
} // namespace Klip::Tests
