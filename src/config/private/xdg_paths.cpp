#include "config/xdg_paths.hpp"

#include "json/files.hpp"
#include "log.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdlib>
#include <expected>
#include <string>
#include <string_view>
#include <system_error>

namespace Klip::Config
{
namespace
{
constexpr std::string_view VideosKey{ "XDG_VIDEOS_DIR=" };
constexpr std::string_view HomeVariable{ "$HOME" };
constexpr size_t MaxUserDirsSize{ size_t{ 64 } * 1024 };

//////////////////////////////////////////////////////////////////////////
std::filesystem::path GetAbsoluteVariable(char const* pName)
{
	char const* const pValue{ std::getenv(pName) };
	std::filesystem::path const value{ (pValue != nullptr) ? pValue : "" };

	return value.is_absolute() ? value : std::filesystem::path{};
}

//////////////////////////////////////////////////////////////////////////
// Qt's reading of user-dirs.dirs, quirks included: the last line wins and one trailing slash goes.
std::string FindVideosDirectory(std::string_view text, std::string_view home)
{
	std::string found{};
	size_t start{ 0 };

	while (start < text.size())
	{
		size_t const end{ std::min(text.find('\n', start), text.size()) };
		std::string_view const line{ text.substr(start, end - start) };

		if (line.starts_with(VideosKey))
		{
			std::string_view value{ line.substr(VideosKey.size()) };

			if (value.size() > 2 && value.starts_with('"') && value.ends_with('"'))
			{
				value = value.substr(1, value.size() - 2);
			}

			if (value.starts_with(HomeVariable))
			{
				found = std::string{ home } + std::string{ value.substr(HomeVariable.size()) };
			}
			else
			{
				found = value;
			}

			if (found.size() > 1 && found.ends_with('/'))
			{
				found.pop_back();
			}
		}

		start = end + 1;
	}

	return found;
}
} // namespace

//////////////////////////////////////////////////////////////////////////
std::filesystem::path GetHome()
{
	return GetAbsoluteVariable("HOME");
}

//////////////////////////////////////////////////////////////////////////
// The specification ignores a relative value, so a relative override falls back exactly like an unset one.
std::filesystem::path GetConfigHome()
{
	std::filesystem::path configHome{ GetAbsoluteVariable("XDG_CONFIG_HOME") };
	std::filesystem::path const home{ GetHome() };

	if (configHome.empty() && !home.empty())
	{
		configHome = home / ".config";
	}

	return configHome;
}

//////////////////////////////////////////////////////////////////////////
std::filesystem::path GetVideosDirectory(std::filesystem::path const& configHome, std::filesystem::path const& home)
{
	std::filesystem::path const fallback{ home / "Videos" };
	std::string found{};

	if (configHome.is_absolute())
	{
		std::filesystem::path const userDirs{ configHome / "user-dirs.dirs" };
		std::expected<std::string, std::error_code> const text{ Json::ReadFile(userDirs, MaxUserDirsSize) };

		if (text.has_value())
		{
			found = FindVideosDirectory(*text, home.string());
		}
		else if (text.error() != std::errc::no_such_file_or_directory)
		{
			gLog.Warning("Cannot read '{}', so the videos directory is taken to be '{}': {}", userDirs.string(),
			             fallback.string(), text.error().message());
		}
	}

	return found.empty() ? fallback : std::filesystem::path{ found };
}
} // namespace Klip::Config
