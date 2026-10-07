#pragma once

#include <cstdint>
#include <functional>
#include <string>

namespace Klip::Desktop
{
enum class ERequest : uint8_t
{
	Toggle,
	Show,
	Quit,

	// Comes before the Show it sanctions: a compositor refuses a raise it was not given a token for.
	ActivationToken
};

struct SRequest final
{
	ERequest    kind{ ERequest::Show };
	std::string token;
};

// Fires on the bus thread.
using RequestCallback = std::function<void(SRequest const&)>;
} // namespace Klip::Desktop
