#pragma once

#include "recorder/settings.hpp"
#include "recorder/settings_changes.hpp"

#include <tge/non_copyable.hpp>

#include <filesystem>
#include <string>
#include <string_view>

namespace Klip::Config
{
class CSettingsStore final : private Tge::SNoCopyNoMove
{
public:

	CSettingsStore() = default;
	~CSettingsStore() = default;

	void Initialize(std::filesystem::path const& configHome, std::filesystem::path const& home);
	Recorder::SSettings Load();
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
