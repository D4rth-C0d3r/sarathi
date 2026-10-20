// PCF8574 I/O expander shared by the display power sequence and the knob button.
// Call it only from the UI task, like everything else on the I2C bus.

#pragma once

#include <cstdint>

namespace sarathi::board::expander {

// Starts I2C and the expander with the board's pin directions. Returns false if the PCF8574
// doesn't answer.
bool Begin();

void Write(uint8_t pin, bool high);

// Reads the pin now (refreshing the input latch), as Elecrow's code does.
bool Read(uint8_t pin);

}  // namespace sarathi::board::expander
