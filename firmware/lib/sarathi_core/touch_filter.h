// Touch coordinate correction and jitter filter. Pure logic.
//
// Follows Elecrow's my_touchpad_read() (vendor/crowpanel-2.1/example/Arduino/
// RotaryScreen_2_1/RotaryScreen_2_1.ino): subtract a Y offset, clamp to the screen, and hold
// the last point until the finger moves by at least the jitter threshold.

#pragma once

#include <cstdint>

namespace sarathi::core {

struct TouchPoint {
  int16_t x;
  int16_t y;
};

class TouchFilter {
 public:
  TouchFilter(int16_t width, int16_t height, int16_t y_offset, int16_t jitter_px);

  // Returns the corrected, de-jittered point for a raw controller sample.
  TouchPoint Apply(int16_t raw_x, int16_t raw_y);

  // Call when the finger lifts, so the next touch starts fresh.
  void Release() { holding_ = false; }

 private:
  int16_t width_;
  int16_t height_;
  int16_t y_offset_;
  int16_t jitter_px_;
  bool holding_ = false;
  TouchPoint held_{0, 0};
};

}  // namespace sarathi::core
