// When a freshly updated image marks itself valid (ADR-0004 §5). Until it does, any reset
// makes the bootloader roll back to the previous image.

#pragma once

#include <cstdint>

namespace sarathi::core {

// How long the UI must have been running before the image is trusted.
constexpr uint32_t kBootSettleMs = 10000;

// TODO(Phase 3): true once sarathi_link (BLE) exists and reports when it is advertising.
constexpr bool kBootNeedsBle = false;

struct BootHealth {
  bool pending_verify = false;  // the bootloader is waiting for this image to confirm itself
  bool ui_ready = false;        // the first frame has been flushed to the panel
  bool ble_ready = false;       // BLE is advertising
  uint32_t uptime_ms = 0;
};

enum class BootDecision : uint8_t {
  kNotPending,  // nothing to confirm (USB-flashed, or already confirmed)
  kWait,
  kConfirm,  // call esp_ota_mark_app_valid_cancel_rollback()
};

[[nodiscard]] BootDecision DecideBoot(const BootHealth& health, bool needs_ble = kBootNeedsBle);

}  // namespace sarathi::core
