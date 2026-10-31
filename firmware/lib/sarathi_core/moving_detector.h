// Decides "moving" from a stream of speed samples (pose, protocol.md section 5), with
// hysteresis so GPS jitter at a standstill doesn't lock the settings: moving once speed stays
// at or above move_kmh for move_s, stopped once it stays below stop_kmh for stop_s. Samples that
// stop arriving make the speed unknown. Pure logic; the thresholds come from the phone (`lock`).

#pragma once

#include <cstdint>

namespace sarathi::core {

// Without a sample for this long, the speed is unknown (protocol.md section 5).
constexpr uint32_t kSpeedTimeoutMs = 5000;

struct LockConfig {
  bool enabled = true;
  uint8_t move_kmh = 5;
  uint8_t move_s = 2;
  uint8_t stop_kmh = 3;  // below move_kmh
  uint8_t stop_s = 5;
};

class MovingDetector {
 public:
  // New thresholds; the current state is kept.
  void Configure(const LockConfig& config) { config_ = config; }
  [[nodiscard]] const LockConfig& Config() const { return config_; }

  // A speed sample; returns whether moving.
  bool Update(uint16_t speed_kmh, uint32_t now_ms);

  // True if samples are arriving (one within kSpeedTimeoutMs).
  [[nodiscard]] bool Fresh(uint32_t now_ms) const {
    return has_sample_ && now_ms - last_sample_ms_ < kSpeedTimeoutMs;
  }

  [[nodiscard]] bool Moving() const { return moving_; }

  // Forgets the state (a lost link, or samples that stopped): not moving, nothing pending.
  void Reset();

 private:
  LockConfig config_;
  bool moving_ = false;
  bool pending_ = false;  // the speed has crossed the threshold to change state
  uint32_t pending_since_ms_ = 0;
  bool has_sample_ = false;
  uint32_t last_sample_ms_ = 0;
};

}  // namespace sarathi::core
