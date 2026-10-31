#include "moving_detector.h"

namespace sarathi::core {
namespace {

constexpr uint32_t kMsPerSecond = 1000;

}  // namespace

bool MovingDetector::Update(uint16_t speed_kmh, uint32_t now_ms) {
  has_sample_ = true;
  last_sample_ms_ = now_ms;
  // Past the threshold that would change the state; between the two, the state holds.
  const bool crossing = moving_ ? speed_kmh < config_.stop_kmh : speed_kmh >= config_.move_kmh;
  if (!crossing) {
    pending_ = false;
    return moving_;
  }
  if (!pending_) {
    pending_ = true;
    pending_since_ms_ = now_ms;
  }
  const uint32_t hold_ms = (moving_ ? config_.stop_s : config_.move_s) * kMsPerSecond;
  if (now_ms - pending_since_ms_ >= hold_ms) {
    moving_ = !moving_;
    pending_ = false;
  }
  return moving_;
}

void MovingDetector::Reset() {
  moving_ = false;
  pending_ = false;
  has_sample_ = false;
}

}  // namespace sarathi::core
