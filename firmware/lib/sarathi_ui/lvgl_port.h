// LVGL display and pointer-input setup for the device. Hardware and platform access is passed
// in as callbacks, so sarathi_ui stays free of Arduino, ESP-IDF and sarathi_board and builds
// in the simulator; main.cpp wires the two together.

#pragma once

#include <cstddef>
#include <cstdint>

namespace sarathi::ui::lvgl_port {

// Copies an RGB565 block to the panel. May modify `pixels`.
using FlushFn = void (*)(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t* pixels);
// Returns true and the screen position while touched, false otherwise.
using TouchFn = bool (*)(int16_t& x, int16_t& y);
// Milliseconds since boot.
using TickFn = uint32_t (*)();
// Allocates a draw buffer once at init; returns nullptr on failure.
using AllocFn = void* (*)(size_t bytes);

// Initialises LVGL, allocates two full-screen draw buffers with `alloc` (once), and
// registers the display and touch input. Call once from the UI task. Returns false if a
// buffer can't be allocated.
bool Init(int16_t width, int16_t height, FlushFn flush, TouchFn touch, TickFn tick, AllocFn alloc);

// Size of each draw buffer Init() allocates, for logging.
size_t BufferBytes(int16_t width, int16_t height);

// Runs LVGL timers, input and rendering. Call often from the UI task only.
void Run();

}  // namespace sarathi::ui::lvgl_port
