// Rotary knob: A/B on GPIO, sampled by a task on core 0; button on PCF8574 P5.
//
// The sampling task only touches GPIO and a queue. The UI task drains steps with TakeStep()
// and reads the button with ReadButtonRaw() (I2C stays on the UI task).

#pragma once

#include <cstdint>

namespace sarathi::board {

class Knob {
 public:
  // Configures the pins and starts the sampling task. Call once. Returns false if the queue
  // or task can't be created.
  bool Begin();

  // Non-blocking. Returns true and sets `step` to +1 or -1 for each detent turned.
  bool TakeStep(int8_t& step);

  // Raw (undebounced) button level, true = pressed. Reads the PCF8574 over I2C.
  bool ReadButtonRaw();

  // Smallest amount of the sampling task's stack that has stayed unused, for the perf log.
  [[nodiscard]] uint32_t StackFreeBytes() const;
};

}  // namespace sarathi::board
