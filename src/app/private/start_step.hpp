#pragma once

#include <cstdint>

namespace Klip
{
enum class EStartStep : uint8_t
{
	None,
	SettlingBeforeBackdrop,
	AwaitingBackdrop,
	Picking,
	SettlingAfterPicker,
	SettlingBeforeCapture
};
} // namespace Klip
