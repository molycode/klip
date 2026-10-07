#pragma once

#include <cstdint>

namespace Klip
{
enum class EFolderDialogState : uint8_t
{
	Closed,
	Open,
	Picked,
	Cancelled,
	Failed
};
} // namespace Klip
