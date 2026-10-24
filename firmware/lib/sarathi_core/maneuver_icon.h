// Drawing the phone's maneuver icon (Chronos, ADR-0005): the 48x48 1-bit picture as runs of
// set pixels per row, so the UI fills a few rectangles at any whole-pixel scale instead of
// keeping an image buffer.

#pragma once

#include <cstddef>
#include <cstdint>

#include "app_state.h"

namespace sarathi::core {

struct IconRun {
  uint8_t y = 0;
  uint8_t x = 0;       // first set pixel
  uint8_t length = 0;  // set pixels in a row
};

[[nodiscard]] inline bool IconPixel(const ManeuverIcon& icon, size_t x, size_t y) {
  const size_t bit = (y * kManeuverIconSize) + x;
  return ((icon.bits.at(bit / 8) >> (7U - (bit % 8U))) & 1U) != 0;
}

// Calls `f(run)` for every run, top to bottom, left to right.
template <typename F>
void ForEachIconRun(const ManeuverIcon& icon, F&& f) {
  for (size_t y = 0; y < kManeuverIconSize; ++y) {
    size_t x = 0;
    while (x < kManeuverIconSize) {
      if (!IconPixel(icon, x, y)) {
        ++x;
        continue;
      }
      const size_t start = x;
      while (x < kManeuverIconSize && IconPixel(icon, x, y)) {
        ++x;
      }
      f(IconRun{static_cast<uint8_t>(y), static_cast<uint8_t>(start),
                static_cast<uint8_t>(x - start)});
    }
  }
}

}  // namespace sarathi::core
