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

	ActivationToken
};

struct SRequest final
{
	ERequest    kind{ ERequest::Show };
	std::string token;
};

using RequestCallback = std::function<void(SRequest const&)>;
} // namespace Klip::Desktop
