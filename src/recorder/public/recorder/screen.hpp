#pragma once

#include <cstdint>

namespace Klip::Recorder
{
struct SScreen final
{
	uint32_t width{ 0 };
	uint32_t height{ 0 };
	uint32_t refreshHz{ 0 };
};
} // namespace Klip::Recorder
