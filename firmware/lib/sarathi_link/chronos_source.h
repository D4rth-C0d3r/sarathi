// Interim phone link through the Chronos app (fbiego/chronos-esp32, ADR-0005): a DataSource
// like the demo ride. Built only with -DSARATHI_BACKEND_CHRONOS (env crowpanel21-chronos);
// Chronos and NimBLE types never leave chronos_source.cpp.
//
// Threads:
// - NimBLE's host task: our RX callback copies each write into a queue. Nothing else.
// - The link task (core 0): runs the library's loop() and its blocking sends (music
//   commands), and stops or restarts BLE.
// - The UI task: Poll() decodes the queued writes and updates the model.

#pragma once

#include <cstdint>

#include "chronos_bridge.h"
#include "chronos_packets.h"
#include "data_source.h"

namespace sarathi::link {

class ChronosSource final : public core::DataSource {
 public:
  // Starts BLE advertising as `name` and the link task. Call once, from setup().
  bool Begin(const char* name);

  // Stops BLE (true) or restarts it (false), so Wi-Fi has the RAM (ADR-0005). Takes effect on
  // the link task; BleStopped() reports when it has.
  void SetBlePaused(bool paused);
  [[nodiscard]] bool BleStopped() const;

  // Advertising or connected: the boot self-check's "BLE up" (ADR-0004 §5).
  [[nodiscard]] bool BleUp() const;

  void Poll(uint32_t now_ms, core::AppModel& model) override;
  void OnCommand(const core::Command& command, uint32_t now_ms, core::AppModel& model) override;

 private:
  ChronosReceiver receiver_;
  ChronosBridge bridge_;
  bool started_ = false;
  bool connected_ = false;
  uint32_t dropped_logged_ = 0;
};

}  // namespace sarathi::link
