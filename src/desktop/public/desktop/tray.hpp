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
// (iiay) as the StatusNotifierItem specification marshals an icon: ARGB32, network byte order.
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

// org.kde.StatusNotifierItem with its com.canonical.dbusmenu, on Bus::gConnection's thread. By hand rather than
// through a toolkit's tray because only this interface carries XAyatanaLabel, the text a panel shows beside the
// icon.
class CTray final : private Tge::SNoCopyNoMove
{
public:

	CTray() = default;
	~CTray() = default;

	// Publishes the item whether or not a panel takes it; IsAvailable says whether one did.
	bool Initialize(STrayIcons icons, RequestCallback onRequest);
	void Terminate();

	bool IsAvailable() const { return m_available; }

	void SetRecording(bool recording);
	void SetLabel(std::string label);
	void SetDetail(std::string detail);

private:

	// Function-local statics: sd-bus keeps the pointer, and member scope lets their lambdas reach the handlers.
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

	// The bus thread's alone, but for m_available, written inside Initialize's wait.
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
