// NOLINTBEGIN(cppcoreguidelines-pro-bounds-pointer-arithmetic)
// Reads a caller-provided byte buffer; bounds come from `size`.

#include "settings_codec.h"

#include "backlight_level.h"

namespace sarathi::core {
namespace {

enum Field : size_t {
  kVersion = 0,
  kTheme = 1,
  kBrightness = 2,
  kAutoDayNight = 3,
  kUnits = 4,
  kScreenTimeout = 5,
  kKnobReversed = 6,
};

bool DecodeBool(uint8_t value, bool fallback) { return value <= 1 ? value == 1 : fallback; }

}  // namespace

SettingsBlob EncodeSettings(const Settings& s) {
  SettingsBlob blob{};
  blob[kVersion] = kSettingsVersion;
  blob[kTheme] = static_cast<uint8_t>(s.theme);
  blob[kBrightness] = static_cast<uint8_t>(ClampBacklightPercent(s.brightness_percent));
  blob[kAutoDayNight] = s.auto_day_night ? 1 : 0;
  blob[kUnits] = static_cast<uint8_t>(s.units);
  blob[kScreenTimeout] = static_cast<uint8_t>(s.screen_timeout);
  blob[kKnobReversed] = s.knob_reversed ? 1 : 0;
  return blob;
}

Settings DecodeSettings(const uint8_t* data, size_t size) {
  const Settings defaults;
  if (data == nullptr || size != kSettingsBlobSize || data[kVersion] != kSettingsVersion) {
    return defaults;
  }
  Settings s = defaults;
  if (data[kTheme] < kThemeCount) {
    s.theme = static_cast<ThemeId>(data[kTheme]);
  }
  if (data[kBrightness] >= kMinBacklightPercent && data[kBrightness] <= kMaxBacklightPercent) {
    s.brightness_percent = data[kBrightness];
  }
  s.auto_day_night = DecodeBool(data[kAutoDayNight], defaults.auto_day_night);
  if (data[kUnits] <= static_cast<uint8_t>(Units::kImperial)) {
    s.units = static_cast<Units>(data[kUnits]);
  }
  if (data[kScreenTimeout] < kScreenTimeoutCount) {
    s.screen_timeout = static_cast<ScreenTimeout>(data[kScreenTimeout]);
  }
  s.knob_reversed = DecodeBool(data[kKnobReversed], defaults.knob_reversed);
  return s;
}

}  // namespace sarathi::core

// NOLINTEND(cppcoreguidelines-pro-bounds-pointer-arithmetic)
