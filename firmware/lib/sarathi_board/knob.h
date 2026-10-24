// Rotary knob: A/B on GPIO and the button on PCF8574 P5, all sampled by a task on core 0.
//
// The task queues detent steps and button level changes with their times; the UI task drains
// both. Replaying the button changes into the debouncer gives the gestures continuous
// sampling would, however long a UI frame takes. Wire locks the bus, so the task's I2C reads
// and the touch reads on the UI task don't collide.

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

  // Raw (undebounced) button levels in time order, true = pressed. Each change comes as two
  // samples at the time it was seen: the old level, then the new one.
  struct ButtonSample {
    bool pressed;
    uint32_t at_ms;
  };
  bool TakeButtonSample(ButtonSample& sample);

  // The latest raw button level the task has seen. No I2C.
  [[nodiscard]] bool ButtonPressed() const;

  // Smallest amount of the sampling task's stack that has stayed unused, for the perf log.
  [[nodiscard]] uint32_t StackFreeBytes() const;
};

}  // namespace sarathi::board
