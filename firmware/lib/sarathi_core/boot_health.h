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
  // Test option: the knob button has been held since power-on. At the confirmation point
  // this counts as a failed self-check, so rollback can be proven on the device.
  bool fail_self_check = false;
  uint32_t uptime_ms = 0;
};

enum class BootDecision : uint8_t {
  kNotPending,  // nothing to confirm (USB-flashed, or already confirmed)
  kWait,
  kConfirm,   // call esp_ota_mark_app_valid_cancel_rollback()
  kRollBack,  // the self-check failed: esp_ota_mark_app_invalid_rollback_and_reboot()
};

[[nodiscard]] BootDecision DecideBoot(const BootHealth& health, bool needs_ble = kBootNeedsBle);

}  // namespace sarathi::core
