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
  // No modem power-save while updating: with some routers a sleeping station misses incoming
  // packets (here, servers' replies to a connection request), so connections time out.
  WiFi.setSleep(false);
  WiFi.begin(credentials.ssid.CStr(),
             credentials.password.Empty() ? nullptr : credentials.password.CStr());
  for (uint32_t waited = 0; waited < timeout_ms; waited += kPollMs) {
    if (WiFi.status() == WL_CONNECTED) {
      const IPAddress ip = WiFi.localIP();
      const IPAddress gateway = WiFi.gatewayIP();
      const IPAddress dns = WiFi.dnsIP();
      ESP_LOGI(kTag, "connected, RSSI %d dBm, IP %u.%u.%u.%u, gateway %u.%u.%u.%u, DNS %u.%u.%u.%u",
               static_cast<int>(WiFi.RSSI()), ip[0], ip[1], ip[2], ip[3], gateway[0], gateway[1],
               gateway[2], gateway[3], dns[0], dns[1], dns[2], dns[3]);
      return true;
    }
    vTaskDelay(pdMS_TO_TICKS(kPollMs));
  }
  ESP_LOGW(kTag, "could not join the saved network (status %d)", static_cast<int>(WiFi.status()));
  WifiOff();
  return false;
}

int WifiRssi() { return WiFi.status() == WL_CONNECTED ? static_cast<int>(WiFi.RSSI()) : 0; }

void WifiOff() {
  WiFi.disconnect(true, false);  // radio off; don't erase the (unused) driver store
  WiFi.mode(WIFI_OFF);
}

}  // namespace sarathi::ota
