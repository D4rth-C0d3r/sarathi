// Quadrature decoder for the rotary knob. Pure logic; the caller samples the A/B pins.
//
// The transition table is Elecrow's (vendor/crowpanel-2.1/example/Arduino/Examples/
// Lesson02-Rotate/Lesson02-Rotate.ino, trans[16]).

#pragma once

#include <cstdint>

namespace sarathi::core {

class KnobDecoder {
 public:
  explicit KnobDecoder(uint8_t transitions_per_detent);

  // Sets the starting pin state without producing a step.
  void Reset(bool a, bool b);

  // Feeds one A/B sample. Returns +1 or -1 when a full detent completes, otherwise 0.
  int8_t Update(bool a, bool b);

 private:
  uint8_t transitions_per_detent_;
  uint8_t state_ = 0;
  int8_t accumulated_ = 0;
};

}  // namespace sarathi::core
