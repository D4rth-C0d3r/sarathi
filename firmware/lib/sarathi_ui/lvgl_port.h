// LVGL display and pointer-input setup. Hardware access is passed in as callbacks, so this
// module doesn't depend on sarathi_board; main.cpp wires the two together.

#pragma once

#include <cstdint>

namespace sarathi::ui::lvgl_port {

// Copies an RGB565 block to the panel. May modify `pixels`.
using FlushFn = void (*)(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t* pixels);
// Returns true and the screen position while touched, false otherwise.
using TouchFn = bool (*)(int16_t& x, int16_t& y);

// Initialises LVGL, allocates two full-screen draw buffers in PSRAM (once), and registers
// the display and touch input. Call once from the UI task. Returns false on allocation
// failure.
bool Init(int16_t width, int16_t height, FlushFn flush, TouchFn touch);

// Runs LVGL timers, input and rendering. Call often from the UI task only.
void Run();

}  // namespace sarathi::ui::lvgl_port
