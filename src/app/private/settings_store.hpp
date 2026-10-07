#pragma once

#include "recorder/settings.hpp"
#include "recorder/settings_changes.hpp"

namespace Klip
{
// Klip.conf through QSettings, which the smoke test reads and writes too: the keys and their values are a
// contract.
Recorder::SSettings LoadSettings();
void                SaveSettings(Recorder::SSettings const& settings, Recorder::SSettingsChanges const& changes);
} // namespace Klip
