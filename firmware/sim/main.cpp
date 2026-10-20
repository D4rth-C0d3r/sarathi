// sārathi desktop simulator: runs the same sarathi_ui shell and sarathi_core logic (with the
// demo ride) as the device, on LVGL's SDL2 driver at 480x480 with the round glass masked off.
//
// Input, translated into what the hardware produces:
//   Left/Down arrow, wheel down  -> knob step -1
//   Right/Up arrow, wheel up     -> knob step +1
//   Enter (held)                 -> knob button (click, double-click, hold = long press)
//   Mouse button inside circle   -> touch at that point
//   S                            -> pause/resume the demo ride (the bike stops; settings unlock)
// Usage: program [--speed N]   replay the ride N times faster. Close the window to quit.
//
// Simulator wiring only: no backlight, panel timing or ESP32-S3 performance limits.

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#include <SDL2/SDL.h>
#include <lvgl.h>

#include "app_model.h"
#include "app_shell.h"
#include "board_pins.h"
#include "demo_ride.h"
#include "input_test_model.h"

namespace {

namespace core = sarathi::core;
namespace ui = sarathi::ui;
namespace pins = sarathi::board::pins;

constexpr int32_t kSize = pins::kScreenWidth;
constexpr int32_t kCenter = kSize / 2;
constexpr int32_t kRadius = kSize / 2;
constexpr uint32_t kLoopDelayMs = 5;

// NOLINTBEGIN(cppcoreguidelines-avoid-non-const-global-variables)
core::AppModel app;
core::DemoRide demo_ride;
core::InputTestModel input_test(pins::kKnobDebounceMs, pins::kKnobDoubleClickMs,
                                pins::kBacklightDefaultPercent, core::kBacklightStepPercent);
int pending_steps = 0;  // written by the SDL event watch, drained by the loop
bool pause_toggle_requested = false;
std::vector<uint8_t> mask_pixels;  // ARGB8888, allocated once at startup
// NOLINTEND(cppcoreguidelines-avoid-non-const-global-variables)

bool InsideCircle(int32_t x, int32_t y) {
  const int32_t dx = x - kCenter;
  const int32_t dy = y - kCenter;
  return (dx * dx) + (dy * dy) <= kRadius * kRadius;
}

// LVGL's SDL driver drains the SDL event queue itself, so key and wheel events are observed
// with an event watch instead of being taken from the queue. Runs on the main thread during
// lv_timer_handler()'s event pump.
int WatchInput(void* /*unused*/, SDL_Event* event) {
  if (event->type == SDL_KEYDOWN && event->key.repeat == 0) {
    switch (event->key.keysym.sym) {
      case SDLK_LEFT:
      case SDLK_DOWN:
        --pending_steps;
        break;
      case SDLK_RIGHT:
      case SDLK_UP:
        ++pending_steps;
        break;
      case SDLK_s:
        pause_toggle_requested = true;
        break;
      default:
        break;
    }
  } else if (event->type == SDL_MOUSEWHEEL) {
    const int notches =
        event->wheel.direction == SDL_MOUSEWHEEL_FLIPPED ? -event->wheel.y : event->wheel.y;
    pending_steps += notches;
  }
  return 0;
}

bool EnterHeld() {
  const Uint8* keys = SDL_GetKeyboardState(nullptr);
  return keys[SDL_SCANCODE_RETURN] != 0 || keys[SDL_SCANCODE_KP_ENTER] != 0;
}

// Mouse as touch. Presses outside the circle are ignored, like the round glass.
void ReadTouch(lv_indev_t* /*indev*/, lv_indev_data_t* data) {
  int x = 0;
  int y = 0;
  const Uint32 buttons = SDL_GetMouseState(&x, &y);
  const bool pressed = SDL_GetMouseFocus() != nullptr &&
                       (buttons & SDL_BUTTON(SDL_BUTTON_LEFT)) != 0 && InsideCircle(x, y);
  ui::app_shell::OnTouch(pressed, static_cast<int16_t>(x), static_cast<int16_t>(y));
  if (pressed) {
    data->point.x = x;
    data->point.y = y;
    data->state = LV_INDEV_STATE_PRESSED;
  } else {
    data->state = LV_INDEV_STATE_RELEASED;
  }
}

// Black outside the visible circle, transparent inside, with a one-pixel soft edge. Drawn on
// LVGL's system layer, above every screen.
void CreateRoundMask() {
  constexpr int32_t kBytesPerPixel = 4;  // ARGB8888, stored as B, G, R, A
  mask_pixels.assign(static_cast<size_t>(kSize) * kSize * kBytesPerPixel, 0);
  for (int32_t y = 0; y < kSize; ++y) {
    for (int32_t x = 0; x < kSize; ++x) {
      const float dx = static_cast<float>(x - kCenter) + 0.5F;
      const float dy = static_cast<float>(y - kCenter) + 0.5F;
      const float outside = SDL_sqrtf((dx * dx) + (dy * dy)) - static_cast<float>(kRadius);
      const float coverage = outside <= 0.0F ? 0.0F : (outside >= 1.0F ? 1.0F : outside);
      mask_pixels[(static_cast<size_t>(y) * kSize + x) * kBytesPerPixel + 3] =
          static_cast<uint8_t>(coverage * 255.0F);
    }
  }
  lv_obj_t* canvas = lv_canvas_create(lv_layer_sys());
  lv_canvas_set_buffer(canvas, mask_pixels.data(), kSize, kSize, LV_COLOR_FORMAT_ARGB8888);
  lv_obj_set_pos(canvas, 0, 0);
  lv_obj_remove_flag(canvas, LV_OBJ_FLAG_CLICKABLE);
}

uint32_t ParseSpeed(int argc, char** argv) {
  for (int i = 1; i + 1 < argc; ++i) {
    if (std::strcmp(argv[i], "--speed") == 0) {                  // NOLINT(*-pointer-arithmetic)
      const long speed = std::strtol(argv[i + 1], nullptr, 10);  // NOLINT(*-pointer-arithmetic)
      return speed > 0 ? static_cast<uint32_t>(speed) : 1U;
    }
  }
  return 1;
}

}  // namespace

int main(int argc, char** argv) {
  const uint32_t speed = ParseSpeed(argc, argv);
  demo_ride.SetSpeedFactor(speed);

  lv_init();
  lv_tick_set_cb(SDL_GetTicks);

  lv_display_t* display = lv_sdl_window_create(kSize, kSize);
  if (display == nullptr) {
    std::fprintf(stderr, "sim: could not open the SDL window\n");
    return 1;
  }
  lv_sdl_window_set_title(display, "sārathi sim");
  SDL_AddEventWatch(WatchInput, nullptr);

  lv_indev_t* touch = lv_indev_create();
  lv_indev_set_type(touch, LV_INDEV_TYPE_POINTER);
  lv_indev_set_read_cb(touch, ReadTouch);
  lv_indev_set_display(touch, display);

  ui::app_shell::Init(app, input_test, pins::kKnobDebounceMs, pins::kKnobDoubleClickMs);
  CreateRoundMask();

  std::printf(
      "sārathi sim (ride speed x%u): arrows/wheel = knob, Enter = button (double-click = next "
      "screen, hold = leave input test), mouse = touch, S = pause ride. Close the window to "
      "quit.\n",
      static_cast<unsigned>(speed));

  for (;;) {
    const uint32_t now = SDL_GetTicks();
    demo_ride.Poll(now, app);

    lv_timer_handler();  // renders, reads input, pumps SDL events; exits when the window closes

    if (pause_toggle_requested) {
      pause_toggle_requested = false;
      demo_ride.SetPaused(!demo_ride.Paused());
      std::printf("sim: ride %s\n", demo_ride.Paused() ? "paused" : "resumed");
    }
    while (pending_steps > 0) {
      ui::app_shell::OnKnobStep(1);
      --pending_steps;
    }
    while (pending_steps < 0) {
      ui::app_shell::OnKnobStep(-1);
      ++pending_steps;
    }
    ui::app_shell::OnButtonSample(EnterHeld(), now);
    ui::app_shell::Update(now);

    core::Command command;
    while (app.TakeCommand(command)) {
      demo_ride.OnCommand(command, now, app);
    }

    SDL_Delay(kLoopDelayMs);
  }
}
