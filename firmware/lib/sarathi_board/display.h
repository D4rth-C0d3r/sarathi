// ST7701S RGB display bring-up: board power sequence, panel init and backlight.
//
// Adapted from Elecrow's CrowPanel 2.1" examples (vendor/crowpanel-2.1/example/Arduino/
// RotaryScreen_2_1/RotaryScreen_2_1.ino and Examples/*/Board21.h).

#pragma once

#include <cstdint>

class Arduino_GFX;

namespace sarathi::board {

class Display {
 public:
  // Runs the board power-up order from CLAUDE.md (I2C, PCF8574, LCD power and reset, touch
  // reset, panel init, backlight). Call once. Returns false if the PCF8574 or panel fails.
  bool Begin();

  // Colours are plain RGB565; the panel's R/B order is handled here.
  void Fill(uint16_t rgb565);
  void DrawRing(int16_t cx, int16_t cy, int16_t outer_radius, int16_t thickness, uint16_t rgb565);
  void DrawCentredText(const char* text, int16_t cy, uint8_t scale, uint16_t rgb565);

  // Converts RGB565 to the order the panel expects (see pins::kSwapRedBlue).
  static uint16_t ToPanel(uint16_t rgb565);

  Arduino_GFX& Gfx();
};

}  // namespace sarathi::board
