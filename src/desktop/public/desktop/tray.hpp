#pragma once

#include "desktop/request.hpp"

#include <tge/non_copyable.hpp>

#include <cstdint>
#include <string>
#include <vector>

struct sd_bus;
struct sd_bus_message;
struct sd_bus_slot;
struct sd_bus_vtable;

namespace Klip::Desktop
{
struct STrayImage final
{
	int32_t              width{ 0 };
	int32_t              height{ 0 };
	std::vector<uint8_t> pixels;
};

struct STrayIcons final
{
	STrayImage idle;
	STrayImage recording;
};

class CTray final : private Tge::SNoCopyNoMove
{
public:

	CTray() = default;
	~CTray() = default;

	bool Initialize(STrayIcons icons, RequestCallback onRequest);
	void Terminate();

	bool IsAvailable() const { return m_available; }

	void SetRecording(bool recording);
	void SetLabel(std::string label);
	void SetDetail(std::string detail);

private:

	static sd_bus_vtable const* GetItemVtable();
	static sd_bus_vtable const* GetMenuVtable();

	bool Publish(sd_bus* pBus);

	std::string GetTitle() const;
	int         AppendIcon(sd_bus_message* pReply) const;
	int         AppendToolTip(sd_bus_message* pReply) const;

	int OnRequest(sd_bus_message* pCall, ERequest kind);
	int OnProvideActivationToken(sd_bus_message* pCall);
	int OnGetLayout(sd_bus_message* pCall) const;
	int OnGetGroupProperties(sd_bus_message* pCall) const;
	int OnGetProperty(sd_bus_message* pCall) const;
	int OnEvent(sd_bus_message* pCall);

	void ApplyRecording(sd_bus* pBus, bool recording);
	void ApplyLabel(sd_bus* pBus, std::string label);
	void ApplyDetail(sd_bus* pBus, std::string detail);
	void Emit(sd_bus* pBus, char const* pSignal) const;

	STrayIcons      m_icons;
	RequestCallback m_onRequest;
	std::string     m_serviceName;
	std::string     m_label{ "Klip" };
	std::string     m_detail;
	sd_bus_slot*    m_pItemSlot{ nullptr };
	sd_bus_slot*    m_pMenuSlot{ nullptr };
	bool            m_recording{ false };
	bool            m_available{ false };
};
} // namespace Klip::Desktop
