#include "wifi_link.h"

#include <WiFi.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

namespace sarathi::ota {
namespace {

constexpr const char* kTag = "wifi";
constexpr uint32_t kPollMs = 100;

}  // namespace

bool WifiConnect(const core::WifiCredentials& credentials, uint32_t timeout_ms) {
  WiFi.persistent(false);
  WiFi.setAutoReconnect(false);
  WiFi.mode(WIFI_STA);
  WiFi.begin(credentials.ssid.CStr(),
             credentials.password.Empty() ? nullptr : credentials.password.CStr());
  for (uint32_t waited = 0; waited < timeout_ms; waited += kPollMs) {
    if (WiFi.status() == WL_CONNECTED) {
      ESP_LOGI(kTag, "connected, RSSI %d dBm", static_cast<int>(WiFi.RSSI()));
      return true;
    }
    vTaskDelay(pdMS_TO_TICKS(kPollMs));
  }
  ESP_LOGW(kTag, "could not join the saved network (status %d)", static_cast<int>(WiFi.status()));
  WifiOff();
  return false;
}

void WifiOff() {
  WiFi.disconnect(true, false);  // radio off; don't erase the (unused) driver store
  WiFi.mode(WIFI_OFF);
}

}  // namespace sarathi::ota
