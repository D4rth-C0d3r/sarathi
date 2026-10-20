// Settings in NVS (ESP-IDF non-volatile storage, through Arduino Preferences): one versioned
// blob (core::settings_codec) under one key, so a save is a single write. Writes are rare by
// design (core::SettingsSaver debounces them), because flash writes can disturb the RGB panel.

#pragma once

#include "app_state.h"

namespace sarathi::settings {

class NvsSettings {
 public:
  // Opens the "sarathi" namespace. Returns false if NVS can't be opened.
  bool Begin();

  // The saved settings, or defaults if none are saved (or they can't be read). `found` says
  // whether a saved blob existed.
  core::Settings Load(bool* found = nullptr);

  // Returns false if the write failed.
  bool Save(const core::Settings& settings);
};

}  // namespace sarathi::settings
