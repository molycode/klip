#include "config/settings_store.hpp"

#include "config/xdg_paths.hpp"
#include "json/files.hpp"
#include "klip_conf.hpp"
#include "log.hpp"
#include "settings_json.hpp"

#include <tge/assert.hpp>

#include <cstddef>
#include <expected>
#include <format>
#include <system_error>
#include <utility>

namespace Klip::Config
{
namespace
{
constexpr std::string_view DirectoryName{ "klip" };
constexpr std::string_view FileName{ "config.json" };
constexpr std::string_view LegacyFileName{ "Klip.conf" };
constexpr std::string_view BackupSuffix{ ".bad" };
constexpr std::string_view RecordingsDirectoryName{ "klip-captures" };
constexpr size_t           MaxFileSize{ size_t{ 1024 } * 1024 };
} // namespace

//////////////////////////////////////////////////////////////////////////
// klip/config.json, which the smoke test reads and edits too.
void CSettingsStore::Initialize(std::filesystem::path const& configHome, std::filesystem::path const& home)
{
	m_defaults.directory = (GetVideosDirectory(configHome, home) / RecordingsDirectoryName).string();

	if (configHome.empty())
	{
		gLog.Warning("Settings will not be saved: neither XDG_CONFIG_HOME nor HOME is an absolute path");
	}
	else
	{
		m_path = configHome / DirectoryName / FileName;
		m_legacyPath = configHome / DirectoryName / LegacyFileName;
	}
}

//////////////////////////////////////////////////////////////////////////
Recorder::SSettings CSettingsStore::Load()
{
	Recorder::SSettings settings{ m_defaults };

	m_kept.clear();
	m_canSave = false;

	if (!m_path.empty())
	{
		std::expected<std::string, std::error_code> text{ Json::ReadFile(m_path, MaxFileSize) };

		if (text.has_value())
		{
			std::expected<SSettingsDocument, ESettingsJsonError> const document{ ReadSettingsJson(*text,
			                                                                                      m_defaults) };

			if (document.has_value())
			{
				settings = document->settings;
				m_kept = std::move(*text);
				m_canSave = document->version <= SettingsVersion;

				if (!m_canSave)
				{
					gLog.Warning("'{}' was written by a newer Klip (format {}), so this one will not save over it",
					             m_path.string(), document->version);
				}

				if (document->numInvalid != 0)
				{
					KeepBackup(std::format("{} invalid (first: {})", document->numInvalid, document->firstInvalidPath));
				}
			}
			else if (document.error() == ESettingsJsonError::NotJson)
			{
				MoveAside(std::format("{} ({})", ToString(document.error()), DescribeSettingsSyntaxError(*text)));
			}
			else
			{
				MoveAside(ToString(document.error()));
			}
		}
		else if (text.error() == std::errc::no_such_file_or_directory)
		{
			m_canSave = true;
			settings = Import();
		}
		else
		{
			gLog.Warning("Cannot read the settings file '{}', so it is left alone and defaults are used: {}",
			             m_path.string(), text.error().message());
		}
	}

	m_written = settings;

	return settings;
}

//////////////////////////////////////////////////////////////////////////
// Only what changes names is taken from settings, so a token arriving does not write the device the recorder
// fell back to while the chosen one is missing.
void CSettingsStore::Save(Recorder::SSettings const& settings, Recorder::SSettingsChanges const& changes)
{
	if (m_canSave && (changes.choices || changes.screenToken || changes.windowToken))
	{
		Recorder::SSettings merged{ changes.choices ? settings : m_written };

		merged.screenToken = changes.screenToken ? settings.screenToken : m_written.screenToken;
		merged.windowToken = changes.windowToken ? settings.windowToken : m_written.windowToken;
		Write(merged);
	}
}

//////////////////////////////////////////////////////////////////////////
std::filesystem::path CSettingsStore::GetDirectory() const
{
	return m_path.parent_path();
}

//////////////////////////////////////////////////////////////////////////
// Once: config.json exists from then on, and Klip.conf stays where it is for an older Klip.
Recorder::SSettings CSettingsStore::Import()
{
	Recorder::SSettings settings{ m_defaults };
	std::expected<std::string, std::error_code> const text{ Json::ReadFile(m_legacyPath, MaxFileSize) };

	if (text.has_value())
	{
		std::expected<SSettingsDocument, ESettingsJsonError> const document{
			ReadSettingsJson(ConvertKlipConf(*text), m_defaults)
		};

		TGE_ASSERT(document.has_value(), "Klip.conf converts to a JSON object, whatever it holds");

		if (document.has_value())
		{
			settings = document->settings;

			if (document->numInvalid != 0)
			{
				gLog.Warning("{} settings in '{}' could not be read and keep their defaults (first: {})",
				             document->numInvalid, m_legacyPath.string(), document->firstInvalidPath);
			}
		}

		if (Write(settings))
		{
			gLog.Info("Imported the settings in '{}' into '{}'", m_legacyPath.string(), m_path.string());
		}
	}
	else if (text.error() != std::errc::no_such_file_or_directory)
	{
		gLog.Warning("Cannot read the old settings file '{}', so defaults are used: {}", m_legacyPath.string(),
		             text.error().message());
	}

	return settings;
}

//////////////////////////////////////////////////////////////////////////
// Skipped when the file already says the same; false only when it could not be written.
bool CSettingsStore::Write(Recorder::SSettings const& settings)
{
	std::string text{ WriteSettingsJson(settings, m_kept) };
	bool        isStored{ text == m_kept };

	if (!isStored)
	{
		std::filesystem::path const directory{ m_path.parent_path() };
		std::error_code             error{};

		std::filesystem::create_directories(directory, error);

		if (error.value() == 0)
		{
			std::expected<void, std::string> const written{ Json::WriteFileAtomically(m_path, text) };

			isStored = written.has_value();

			if (isStored)
			{
				m_kept = std::move(text);
			}
			else
			{
				gLog.Error("Cannot save the settings to '{}': {}", m_path.string(), written.error());
			}
		}
		else
		{
			gLog.Error("Cannot save the settings: cannot create '{}': {}", directory.string(), error.message());
		}
	}

	if (isStored)
	{
		m_written = settings;
	}

	return isStored;
}

//////////////////////////////////////////////////////////////////////////
// A copy, since the file stays in use; it is the only place the replaced values survive the repairing save.
void CSettingsStore::KeepBackup(std::string_view problem)
{
	std::filesystem::path const backupPath{ m_path.string() + std::string{ BackupSuffix } };
	std::error_code             error{};

	std::filesystem::copy_file(m_path, backupPath, std::filesystem::copy_options::overwrite_existing, error);

	if (error.value() == 0)
	{
		gLog.Warning("Settings in '{}' were replaced by defaults, {}; the original is kept as '{}'", m_path.string(),
		             problem, backupPath.string());
	}
	else
	{
		m_canSave = false;
		gLog.Warning("Settings in '{}' were replaced by defaults, {}, and the file cannot be backed up, so it is "
		             "left alone: {}",
		             m_path.string(), problem, error.message());
	}
}

//////////////////////////////////////////////////////////////////////////
// Kept rather than overwritten, so a typo in a hand edit never costs every other setting.
void CSettingsStore::MoveAside(std::string_view reason)
{
	std::filesystem::path const movedPath{ m_path.string() + std::string{ BackupSuffix } };
	std::error_code             error{};

	std::filesystem::rename(m_path, movedPath, error);
	m_canSave = error.value() == 0;

	if (m_canSave)
	{
		gLog.Warning("The settings file '{}' is {}; it was moved to '{}' and defaults are used", m_path.string(),
		             reason, movedPath.string());
	}
	else
	{
		gLog.Warning("The settings file '{}' is {}, and it cannot be moved aside, so it is left alone and defaults "
		             "are used: {}",
		             m_path.string(), reason, error.message());
	}
}
} // namespace Klip::Config
