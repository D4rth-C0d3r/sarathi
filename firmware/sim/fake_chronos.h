// A pretend phone running the Chronos app, for `--chronos` (ADR-0005). It writes real Chronos
// packets (the format in fbiego/chronos-esp32 1.9.1) into the same sarathi_link receiver and
// bridge the device uses, so the simulator shows exactly what the Chronos build shows:
// navigation in arrow mode with the phone's icon, a call that can only be dismissed, and
// music without progress.

#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "app_model.h"
#include "chronos_bridge.h"
#include "chronos_packets.h"
#include "data_source.h"

namespace sarathi::sim {

class FakeChronos final : public core::DataSource {
 public:
  void Poll(uint32_t now_ms, core::AppModel& model) override;
  void OnCommand(const core::Command& command, uint32_t now_ms, core::AppModel& model) override;

  // The phone drops the link (or comes back): Nav shows "Phone not connected".
  void SetConnected(bool connected, uint32_t now_ms, core::AppModel& model);

 private:
  void Write(const std::vector<uint8_t>& packet, uint32_t now_ms, core::AppModel& model);
  void SendNav(uint32_t now_ms, core::AppModel& model);
  void SendTrack(uint32_t now_ms, core::AppModel& model);

  link::ChronosReceiver receiver_;
  link::ChronosBridge bridge_;
  bool started_ = false;
  bool connected_ = false;
  uint32_t next_nav_ms_ = 0;
  size_t nav_step_ = 0;
  size_t track_ = 0;
  bool playing_ = true;
};

}  // namespace sarathi::sim
