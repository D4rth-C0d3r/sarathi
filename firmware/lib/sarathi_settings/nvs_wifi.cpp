#include "nvs_wifi.h"

#include <Preferences.h>
#include <esp_log.h>

namespace sarathi::settings {
namespace {

constexpr const char* kTag = "wifi_cfg";
constexpr const char* kNamespace = "wifi";
constexpr const char* kKey = "cfg";

// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
Preferences prefs;
bool opened = false;  // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)

}  // namespace

bool NvsWifi::Begin() {
  opened = prefs.begin(kNamespace, false);
  if (!opened) {
    ESP_LOGE(kTag, "could not open NVS namespace '%s'", kNamespace);
  }
  return opened;
}

bool NvsWifi::Load(core::WifiCredentials* out) {
  core::WifiBlob blob{};
  if (!opened || prefs.getBytesLength(kKey) != blob.size() ||
      prefs.getBytes(kKey, blob.data(), blob.size()) != blob.size()) {
    return false;
  }
  return core::DecodeWifiCredentials(blob.data(), blob.size(), out);
}

bool NvsWifi::Save(const core::WifiCredentials& credentials) {
  if (!opened || !core::ValidSsid(credentials.ssid.View()) ||
      !core::ValidWifiPassword(credentials.password.View())) {
    return false;
  }
  const core::WifiBlob blob = core::EncodeWifiCredentials(credentials);
  const bool ok = prefs.putBytes(kKey, blob.data(), blob.size()) == blob.size();
  // Never log the password, nor the network name (it can identify a home).
  if (ok) {
    ESP_LOGI(kTag, "network saved");
  } else {
    ESP_LOGE(kTag, "saving the network failed");
  }
  return ok;
}

bool NvsWifi::Erase() {
  const bool ok = opened && prefs.remove(kKey);
  if (ok) {
    ESP_LOGI(kTag, "network forgotten");
  }
  return ok;
}

}  // namespace sarathi::settings
