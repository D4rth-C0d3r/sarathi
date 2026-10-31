// Our own phone link (protocol v1, ADR-0007): one GATT service over NimBLE, a DataSource like
// the demo ride. Built only with -DSARATHI_BACKEND_PHONE (env crowpanel21-phone); NimBLE types
// never leave sarathi_source.cpp. The protocol logic is ProtocolBridge (native tests).
//
// Threads (as in the Chronos build):
// - NimBLE's host task: callbacks copy each rx write into a queue and record connection,
//   security and pairing events. No parsing, logging or LVGL there.
// - The link task (core 0): starts and stops BLE (for Wi-Fi), raises the MTU, answers pairing,
//   and fragments and notifies the messages the bridge queued.
// - The UI task: Poll() feeds the frames to the bridge and turns events into app state.

#pragma once

#include <cstdint>

#include "data_source.h"

namespace sarathi::link {

class SarathiSource final : public core::DataSource {
 public:
  // Allocates its buffers (PSRAM), starts BLE advertising as `name` and the link task. Call
  // once, from setup().
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
  void PollLink(core::AppModel& model);
  void PollPairing(uint32_t now_ms, core::AppModel& model);
  void LogStats();
  void LogMotion(const core::AppModel& model);

  bool started_ = false;
  uint32_t generation_seen_ = 0;
  bool connected_seen_ = false;
  bool secured_seen_ = false;
  bool stopped_seen_ = false;
  uint32_t pairing_requests_seen_ = 0;
  uint32_t pairing_ends_seen_ = 0;
  uint32_t refused_logged_ = 0;
  uint32_t dropped_logged_ = 0;
  uint16_t mtu_logged_ = 0;
  // The moving lock as last logged (moving, speed known, lock on), to log each change.
  uint8_t motion_logged_ = 0xFF;
};

}  // namespace sarathi::link
