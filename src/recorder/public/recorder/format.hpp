#pragma once

#include "recorder/throughput.hpp"

#include <chrono>
#include <cstdint>
#include <string>

namespace Klip::Recorder
{
std::string FormatDuration(std::chrono::milliseconds elapsed);
std::string FormatBytes(uint64_t bytes);
SThroughput FormatThroughput(uint64_t bytes, std::chrono::seconds elapsed);
} // namespace Klip::Recorder
