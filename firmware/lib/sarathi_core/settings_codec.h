// Settings <-> a small versioned blob for persistent storage (NVS on the device, a file in the
// simulator). Pure logic. Decoding never fails: anything unreadable falls back to defaults,
// so a corrupt or old blob can't break boot.

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "app_state.h"

namespace sarathi::core {

// Bump when the layout changes, and teach DecodeSettings the old layouts.
constexpr uint8_t kSettingsVersion = 1;

// Layout v1: [0] version, [1] theme, [2] brightness %, [3] auto day/night, [4] units,
// [5] screen timeout, [6] knob reversed, [7] reserved (0).
constexpr size_t kSettingsBlobSize = 8;
using SettingsBlob = std::array<uint8_t, kSettingsBlobSize>;

[[nodiscard]] SettingsBlob EncodeSettings(const Settings& settings);

// A wrong size or unknown version gives all defaults; a single bad field gives that field's
// default.
[[nodiscard]] Settings DecodeSettings(const uint8_t* data, size_t size);

}  // namespace sarathi::core
