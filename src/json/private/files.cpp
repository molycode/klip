#include "json/files.hpp"

#include "log.hpp"

#include <cerrno>
#include <cstdlib>
#include <fcntl.h>
#include <format>
#include <sys/stat.h>
#include <unistd.h>

namespace Klip::Json
{
namespace
{
constexpr std::string_view TemporarySuffix{ ".XXXXXX" };
constexpr mode_t NewFileMode{ 0644 };
constexpr mode_t PermissionBits{ 07777 };

//////////////////////////////////////////////////////////////////////////
std::error_code LastError()
{
	return std::error_code{ errno, std::generic_category() };
}

//////////////////////////////////////////////////////////////////////////
std::error_code WriteAll(int fd, std::string_view text)
{
	std::error_code error{};
	size_t numWritten{ 0 };

	while (error.value() == 0 && numWritten < text.size())
	{
		ssize_t const count{ ::write(fd, text.data() + numWritten, text.size() - numWritten) };

		if (count > 0)
		{
			numWritten += static_cast<size_t>(count);
		}
		else if (count == 0)
		{
			error = std::make_error_code(std::errc::io_error);
		}
		else if (errno != EINTR)
		{
			error = LastError();
		}
	}

	return error;
}

//////////////////////////////////////////////////////////////////////////
// A link a dotfile manager put in the file's place survives, since the rename replaces its target instead.
std::filesystem::path ResolveLink(std::filesystem::path const& path)
{
	std::error_code error{};
	std::filesystem::path const target{ std::filesystem::canonical(path, error) };

	return (error.value() == 0) ? target : path;
}

//////////////////////////////////////////////////////////////////////////
// An existing file keeps its permissions, so one the user restricted stays restricted.
mode_t GetFileMode(std::filesystem::path const& path)
{
	struct stat status{};

	return (::stat(path.c_str(), &status) == 0) ? (status.st_mode & PermissionBits) : NewFileMode;
}
} // namespace

//////////////////////////////////////////////////////////////////////////
// O_NONBLOCK so a FIFO in the file's place cannot hang the caller; it does not affect a regular file.
std::expected<std::string, std::error_code> ReadFile(std::filesystem::path const& path, size_t maxSize)
{
	std::expected<std::string, std::error_code> result{ std::unexpected{ std::error_code{} } };
	int const fd{ ::open(path.c_str(), O_RDONLY | O_CLOEXEC | O_NONBLOCK) };

	if (fd >= 0)
	{
		struct stat status{};

		if (::fstat(fd, &status) != 0)
		{
			result = std::unexpected{ LastError() };
		}
		else if (S_ISDIR(status.st_mode))
		{
			result = std::unexpected{ std::make_error_code(std::errc::is_a_directory) };
		}
		else if (!S_ISREG(status.st_mode))
		{
			result = std::unexpected{ std::make_error_code(std::errc::invalid_argument) };
		}
		else if (static_cast<size_t>(status.st_size) > maxSize)
		{
			result = std::unexpected{ std::make_error_code(std::errc::file_too_large) };
		}
		else
		{
			std::string text{};
			std::error_code readError{};
			size_t numRead{ 0 };
			bool isDone{ false };

			text.resize(static_cast<size_t>(status.st_size));

			while (!isDone)
			{
				ssize_t const count{ ::read(fd, text.data() + numRead, text.size() - numRead) };

				if (count > 0)
				{
					numRead += static_cast<size_t>(count);
					isDone = numRead == text.size();
				}
				else if (count == 0)
				{
					text.resize(numRead);
					isDone = true;
				}
				else if (errno != EINTR)
				{
					readError = LastError();
					isDone = true;
				}
			}

			if (readError.value() == 0)
			{
				result = std::move(text);
			}
			else
			{
				result = std::unexpected{ readError };
			}
		}

		::close(fd);
	}
	else
	{
		result = std::unexpected{ LastError() };
	}

	return result;
}

//////////////////////////////////////////////////////////////////////////
std::expected<void, std::string> WriteFileAtomically(std::filesystem::path const& path, std::string_view text)
{
	std::filesystem::path const target{ ResolveLink(path) };
	std::string temporaryPath{ target.string() + std::string{ TemporarySuffix } };
	std::string failure{};
	int const fd{ ::mkostemp(temporaryPath.data(), O_CLOEXEC) };

	if (fd >= 0)
	{
		std::error_code error{};

		if (::fchmod(fd, GetFileMode(target)) != 0)
		{
			failure = std::format("cannot set the permissions of '{}': {}", temporaryPath,
			                      LastError().message());
		}
		else if (error = WriteAll(fd, text); error.value() != 0)
		{
			failure = std::format("cannot write '{}': {}", temporaryPath, error.message());
		}
		else if (::fsync(fd) != 0)
		{
			failure = std::format("cannot flush '{}': {}", temporaryPath, LastError().message());
		}

		if (::close(fd) != 0 && failure.empty())
		{
			failure = std::format("cannot close '{}': {}", temporaryPath, LastError().message());
		}

		if (failure.empty())
		{
			std::filesystem::rename(temporaryPath, target, error);

			if (error.value() != 0)
			{
				failure = std::format("cannot replace it with '{}': {}", temporaryPath, error.message());
			}
		}

		if (!failure.empty())
		{
			std::error_code removeError{};

			std::filesystem::remove(temporaryPath, removeError);

			if (removeError.value() != 0)
			{
				gLog.Warning("Cannot remove the temporary file '{}': {}", temporaryPath, removeError.message());
			}
		}
	}
	else
	{
		failure = std::format("cannot create a temporary file beside it: {}", LastError().message());
	}

	std::expected<void, std::string> result{};

	if (!failure.empty())
	{
		result = std::unexpected{ std::move(failure) };
	}

	return result;
}
} // namespace Klip::Json
