// BLE and Wi-Fi don't both fit in internal RAM on this board: NimBLE takes about 72 KB and a
// Wi-Fi update about 100 KB (docs/phase3-findings.md). So BLE stops while Wi-Fi is in use,
// during a software update or Wi-Fi setup, and a job that turns Wi-Fi on waits until BLE is
// off (ADR-0005).

#pragma once

#include <cstdint>

#include "ota_update.h"
#include "wifi_setup.h"

namespace sarathi::core {

// Wi-Fi stays off this long before BLE restarts, so the two never overlap while one shuts down.
constexpr uint32_t kBleResumeDelayMs = 3000;

[[nodiscard]] bool WifiInUse(OtaPhase ota, WifiSetupPhase setup);

[[nodiscard]] constexpr bool JobStartsWifi(OtaJobType job) { return job == OtaJobType::kConnect; }

[[nodiscard]] constexpr bool JobStartsWifi(WifiSetupJob job) {
  return job == WifiSetupJob::kStartAccessPoint;
}

class RadioPolicy {
 public:
  // Call every loop. Returns true while BLE should be stopped.
  bool Update(bool wifi_in_use, uint32_t now_ms);

  [[nodiscard]] bool BlePaused() const { return paused_; }

 private:
  bool paused_ = false;
  uint32_t wifi_off_since_ms_ = 0;
};

}  // namespace sarathi::core
