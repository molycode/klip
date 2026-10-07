#pragma once

#include "recorder/settings.hpp"
#include "recorder/settings_changes.hpp"

#include <tge/non_copyable.hpp>

#include <filesystem>
#include <string>
#include <string_view>

namespace Klip::Config
{
// klip/config.json under the config home, which the smoke test reads and edits too. Every problem is logged
// here, so callers only pass settings in and out.
class CSettingsStore final : private Tge::SNoCopyNoMove
{
public:

	CSettingsStore() = default;
	~CSettingsStore() = default;

	// Nothing is saved without a configHome; home is where the default directory falls back to.
	void Initialize(std::filesystem::path const& configHome, std::filesystem::path const& home);
	Recorder::SSettings Load();
	// Only what changes names is taken from settings, so a token arriving does not write the device the
	// recorder fell back to while the chosen one is missing.
	void Save(Recorder::SSettings const& settings, Recorder::SSettingsChanges const& changes);

private:

	Recorder::SSettings Import();
	bool                Write(Recorder::SSettings const& settings);
	void                KeepBackup(std::string_view problem);
	void                MoveAside(std::string_view reason);

	std::filesystem::path m_path;
	std::filesystem::path m_legacyPath;
	Recorder::SSettings   m_defaults;
	Recorder::SSettings   m_written;
	std::string           m_kept;
	bool                  m_canSave{ false };
};
} // namespace Klip::Config
