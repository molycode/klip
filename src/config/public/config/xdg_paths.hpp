#pragma once

#include <filesystem>

namespace Klip::Config
{
// Empty when HOME is unset or not an absolute path.
std::filesystem::path GetHome();
// Empty when neither XDG_CONFIG_HOME nor HOME is an absolute path.
std::filesystem::path GetConfigHome();
// Found as QStandardPaths finds MoviesLocation, which every Klip.conf directory default was made from.
std::filesystem::path GetVideosDirectory(std::filesystem::path const& configHome, std::filesystem::path const& home);
} // namespace Klip::Config
