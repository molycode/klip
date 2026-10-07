#pragma once

#include <filesystem>

namespace Klip::Config
{
std::filesystem::path GetHome();
std::filesystem::path GetConfigHome();
std::filesystem::path GetStateHome();
std::filesystem::path GetVideosDirectory(std::filesystem::path const& configHome, std::filesystem::path const& home);
} // namespace Klip::Config
