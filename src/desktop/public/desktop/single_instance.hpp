#pragma once

#include "desktop/request.hpp"

#include <tge/non_copyable.hpp>

#include <string_view>

struct sd_bus_message;
struct sd_bus_slot;
struct sd_bus_vtable;

namespace Klip::Desktop
{
// One Klip per session. The first owns io.github.molycode.Klip and answers org.freedesktop.Application's
// Activate there; a second finds the name taken and asks the first to show itself instead.
class CSingleInstance final : private Tge::SNoCopyNoMove
{
public:

	CSingleInstance() = default;
	~CSingleInstance() = default;

	// False only when another Klip owns the name: better two windows than none.
	bool Claim();

	// The token is this process's own, which a compositor wants before it lets another window be raised.
	void AskOwnerToShow(std::string_view activationToken);

	void Serve(RequestCallback onRequest);
	void Terminate();

private:

	// A function-local static: sd-bus keeps the pointer, and member scope lets its lambda reach OnActivate.
	static sd_bus_vtable const* GetVtable();

	int OnActivate(sd_bus_message* pCall);

	RequestCallback m_onRequest;
	sd_bus_slot*    m_pSlot{ nullptr };
	bool            m_owned{ false };
};

extern CSingleInstance gSingleInstance;
} // namespace Klip::Desktop
