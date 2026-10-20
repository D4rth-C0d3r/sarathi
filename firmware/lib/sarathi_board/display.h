// ST7701S RGB display bring-up: board power sequence, panel init and backlight.
//
// Adapted from Elecrow's CrowPanel 2.1" examples (vendor/crowpanel-2.1/example/Arduino/
// RotaryScreen_2_1/RotaryScreen_2_1.ino and Examples/*/Board21.h).

#pragma once

#include <cstdint>

namespace sarathi::board {

class Backlight;

class Display {
 public:
  // Runs the board power-up order (see docs/hardware.md): I2C and PCF8574, LCD power and
  // reset, touch reset, panel init, then the backlight at `backlight_percent`. Call once.
  // Returns false if the PCF8574, panel or backlight fails.
  bool Begin(Backlight& backlight, int backlight_percent);

  // Copies a w x h block of RGB565 pixels to (x, y). Swaps R and B in place for the panel,
  // so `pixels` is modified. Safe to call from the LVGL flush callback.
  void DrawBitmap(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t* pixels);
};

}  // namespace sarathi::board
