// CST8xx capacitive touch controller on the shared I2C bus. Call only from the UI task.

#pragma once

#include <cstdint>

namespace sarathi::board {

class Touch {
 public:
  // Call after Display::Begin(), which powers and resets the controller.
  bool Begin();

  // Reads the first touch point as reported by the controller (no offset or filtering).
  // Returns false when nothing is touching the screen.
  bool ReadRaw(int16_t& x, int16_t& y);
};

}  // namespace sarathi::board
