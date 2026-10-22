// Station-mode Wi-Fi for OTA only (ADR-0001: Wi-Fi is for OTA). Blocking: call from the OTA
// worker task. The driver's own credential store is never used (WiFi.persistent(false)), so
// the password lives only in NVS namespace "wifi".

#pragma once

#include <cstdint>

#include "wifi_credentials.h"

namespace sarathi::ota {

constexpr uint32_t kWifiConnectTimeoutMs = 15000;

// Joins the network. Returns false on timeout (the radio is then switched off again).
bool WifiConnect(const core::WifiCredentials& credentials,
                 uint32_t timeout_ms = kWifiConnectTimeoutMs);

// Disconnects and switches the radio off.
void WifiOff();

}  // namespace sarathi::ota
