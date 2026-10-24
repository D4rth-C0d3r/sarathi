#include "nvs_settings.h"

#include <Preferences.h>
#include <esp_log.h>

#include "settings_codec.h"

namespace sarathi::settings {
namespace {

constexpr const char* kTag = "settings";
constexpr const char* kNamespace = "sarathi";
constexpr const char* kKey = "cfg";

// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
Preferences prefs;
bool opened = false;  // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)

}  // namespace

bool NvsSettings::Begin() {
  opened = prefs.begin(kNamespace, false);
  if (!opened) {
    ESP_LOGE(kTag, "could not open NVS namespace '%s'", kNamespace);
  }
  return opened;
}

core::Settings NvsSettings::Load(bool* found) {
  core::SettingsBlob blob{};
  const size_t stored = opened ? prefs.getBytesLength(kKey) : 0;
  const bool exists =
      stored == blob.size() && prefs.getBytes(kKey, blob.data(), blob.size()) == blob.size();
  if (found != nullptr) {
    *found = exists;
  }
  if (!exists) {
    if (stored != 0) {
      ESP_LOGW(kTag, "saved settings have an unexpected size (%u B); using defaults",
               static_cast<unsigned>(stored));
    }
    return core::Settings{};
  }
  return core::DecodeSettings(blob.data(), blob.size());
}

bool NvsSettings::Save(const core::Settings& settings) {
  if (!opened) {
    return false;
  }
  const core::SettingsBlob blob = core::EncodeSettings(settings);
  const bool ok = prefs.putBytes(kKey, blob.data(), blob.size()) == blob.size();
  if (ok) {
    ESP_LOGI(kTag, "settings saved");
  } else {
    ESP_LOGE(kTag, "settings save failed");
  }
  return ok;
}

uint32_t NvsSettings::LoadMarker(const char* key) { return opened ? prefs.getULong(key, 0) : 0; }

void NvsSettings::SaveMarker(const char* key, uint32_t value) {
  if (opened && prefs.getULong(key, 0) != value) {
    prefs.putULong(key, value);
  }
}

}  // namespace sarathi::settings
