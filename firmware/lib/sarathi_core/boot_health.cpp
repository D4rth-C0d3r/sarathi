#include "boot_health.h"

namespace sarathi::core {

BootDecision DecideBoot(const BootHealth& health, bool needs_ble) {
  if (!health.pending_verify) {
    return BootDecision::kNotPending;
  }
  // Wi-Fi is deliberately not a condition: an update out of Wi-Fi range must still stick.
  const bool healthy =
      health.ui_ready && (!needs_ble || health.ble_ready) && health.uptime_ms >= kBootSettleMs;
  return healthy ? BootDecision::kConfirm : BootDecision::kWait;
}

}  // namespace sarathi::core
