#include "touch_filter.h"

#include <algorithm>
#include <cstdlib>

namespace sarathi::core {

TouchFilter::TouchFilter(int16_t width, int16_t height, int16_t y_offset, int16_t jitter_px)
    : width_(width), height_(height), y_offset_(y_offset), jitter_px_(jitter_px) {}

TouchPoint TouchFilter::Apply(int16_t raw_x, int16_t raw_y) {
  const int x = std::clamp<int>(raw_x, 0, width_ - 1);
  const int y = std::clamp<int>(raw_y - y_offset_, 0, height_ - 1);

  if (!holding_ || std::abs(x - held_.x) >= jitter_px_ || std::abs(y - held_.y) >= jitter_px_) {
    held_ = {static_cast<int16_t>(x), static_cast<int16_t>(y)};
    holding_ = true;
  }
  return held_;
}

}  // namespace sarathi::core
