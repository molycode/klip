#pragma once

#include "desktop/request.hpp"

#include <tge/non_copyable.hpp>

#include <string_view>

struct sd_bus_message;
struct sd_bus_slot;
struct sd_bus_vtable;

namespace Klip::Desktop
{
class CSingleInstance final : private Tge::SNoCopyNoMove
{
public:

	CSingleInstance() = default;
	~CSingleInstance() = default;

	bool Claim();

	void AskOwnerToShow(std::string_view activationToken);

	void Serve(RequestCallback onRequest);
	void Terminate();

private:

	static sd_bus_vtable const* GetVtable();

	int OnActivate(sd_bus_message* pCall);

	RequestCallback m_onRequest;
	sd_bus_slot*    m_pSlot{ nullptr };
	bool            m_owned{ false };
};

extern CSingleInstance gSingleInstance;
} // namespace Klip::Desktop
