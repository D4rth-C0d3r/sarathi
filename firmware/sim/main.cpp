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

#include <array>
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

// ---- --tour DIR: a scripted walk through every screen that saves screenshots ----
// It drives the shell exactly like the knob does (button samples and steps), so it doubles as
// an end-to-end check of the UI. Times are from start; the ride runs at 10x, so the call rings
// from 6 s to 8 s. A click reaches LVGL ~400 ms after the press (release + double-click
// window), so the next action waits at least 500 ms.

enum class TourAction : uint8_t { kClick, kDoubleClick, kStepUp, kStepDown, kPause, kShot, kQuit };

struct TourStep {
  uint32_t at_ms;
  TourAction action;
  const char* name;  // screenshot file name (kShot only)
};

constexpr uint32_t kTourSpeed = 10;
constexpr uint32_t kPressMs = 100;
constexpr uint32_t kDoubleGapMs = 200;

constexpr std::array<TourStep, 28> kTour = {{
    {1000, TourAction::kShot, "01_nav_classic"},
    {6400, TourAction::kShot, "02_call_ringing"},
    {6500, TourAction::kStepUp, nullptr},  // focus Reject
    {6700, TourAction::kClick, nullptr},   // reject: back to Nav
    {7600, TourAction::kPause, nullptr},   // stop the bike so Settings unlocks
    {7700, TourAction::kDoubleClick, nullptr},
    {8600, TourAction::kShot, "03_music_classic"},
    {8800, TourAction::kDoubleClick, nullptr},
    {9700, TourAction::kShot, "04_settings_classic"},
    {9900, TourAction::kClick, nullptr},  // Theme row: open the theme list
    {10900, TourAction::kShot, "05_theme_list"},
    {11000, TourAction::kStepUp, nullptr},  // focus Fighter jet HUD
    {11200, TourAction::kClick, nullptr},   // select it: switches at runtime
    {12200, TourAction::kShot, "06_settings_hud"},
    {12300, TourAction::kStepUp, nullptr},  // focus Brightness
    {12500, TourAction::kClick, nullptr},   // start adjusting
    {13400, TourAction::kStepDown, nullptr},
    {13450, TourAction::kStepDown, nullptr},
    {13800, TourAction::kShot, "07_brightness_editing"},
    {13900, TourAction::kClick, nullptr},  // confirm
    {14800, TourAction::kDoubleClick, nullptr},
    {15700, TourAction::kShot, "08_nav_hud"},
    {15900, TourAction::kDoubleClick, nullptr},
    {16700, TourAction::kShot, "09_music_hud"},
    {16900, TourAction::kPause, nullptr},        // resume riding
    {17000, TourAction::kDoubleClick, nullptr},  // Music -> Nav (Settings skipped while moving)
    {18000, TourAction::kShot, "10_nav_hud_moving"},
    {18300, TourAction::kQuit, nullptr},
}};

struct PressWindow {
  uint32_t down_ms = 0;
  uint32_t up_ms = 0;
};

struct Tour {
  const char* dir = nullptr;
  uint32_t start_ms = 0;
  size_t next = 0;
  std::array<PressWindow, 4> presses{};
  size_t press_count = 0;

  void Press(uint32_t at) {
    presses.at(press_count % presses.size()) = {at, at + kPressMs};
    ++press_count;
  }

  [[nodiscard]] bool ButtonDown(uint32_t now) const {
    for (const PressWindow& p : presses) {
      if (now >= p.down_ms && now < p.up_ms) {
        return true;
      }
    }
    return false;
  }
};

void SaveScreenshot(lv_display_t* display, const char* dir, const char* name) {
  lv_refr_now(display);
  auto* renderer = static_cast<SDL_Renderer*>(lv_sdl_window_get_renderer(display));
  SDL_Surface* shot = SDL_CreateRGBSurfaceWithFormat(0, kSize, kSize, 32, SDL_PIXELFORMAT_ARGB8888);
  if (shot == nullptr || renderer == nullptr) {
    std::fprintf(stderr, "sim: screenshot failed: %s\n", SDL_GetError());
    return;
  }
  SDL_RenderReadPixels(renderer, nullptr, SDL_PIXELFORMAT_ARGB8888, shot->pixels, shot->pitch);
  std::array<char, 512> path{};
  std::snprintf(path.data(), path.size(), "%s/%s.bmp", dir, name);
  if (SDL_SaveBMP(shot, path.data()) == 0) {
    std::printf("sim: saved %s\n", path.data());
  } else {
    std::fprintf(stderr, "sim: could not write %s: %s\n", path.data(), SDL_GetError());
  }
  SDL_FreeSurface(shot);
}

// Runs the tour steps that are due. Returns false when the tour has finished.
bool RunTour(Tour& tour, lv_display_t* display, uint32_t now) {
  while (tour.next < kTour.size() && now - tour.start_ms >= kTour.at(tour.next).at_ms) {
    const TourStep& step = kTour.at(tour.next++);
    switch (step.action) {
      case TourAction::kClick:
        tour.Press(now);
        break;
      case TourAction::kDoubleClick:
        tour.Press(now);
        tour.Press(now + kDoubleGapMs);
        break;
      case TourAction::kStepUp:
        ui::app_shell::OnKnobStep(1);
        break;
      case TourAction::kStepDown:
        ui::app_shell::OnKnobStep(-1);
        break;
      case TourAction::kPause:
        demo_ride.SetPaused(!demo_ride.Paused());
        break;
      case TourAction::kShot:
        SaveScreenshot(display, tour.dir, step.name);
        break;
      case TourAction::kQuit:
        return false;
    }
  }
  return true;
}

const char* ParseTourDir(int argc, char** argv) {
  for (int i = 1; i + 1 < argc; ++i) {
    if (std::strcmp(argv[i], "--tour") == 0) {  // NOLINT(*-pointer-arithmetic)
      return argv[i + 1];                       // NOLINT(*-pointer-arithmetic)
    }
  }
  return nullptr;
}

}  // namespace

int main(int argc, char** argv) {
  Tour tour;
  tour.dir = ParseTourDir(argc, argv);
  const uint32_t speed = tour.dir != nullptr ? kTourSpeed : ParseSpeed(argc, argv);
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
  tour.start_ms = SDL_GetTicks();

  for (;;) {
    const uint32_t now = SDL_GetTicks();
    demo_ride.Poll(now, app);

    lv_timer_handler();  // renders, reads input, pumps SDL events; exits when the window closes

    if (tour.dir != nullptr && !RunTour(tour, display, now)) {
      return 0;
    }
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
    ui::app_shell::OnButtonSample(EnterHeld() || tour.ButtonDown(now), now);
    ui::app_shell::Update(now);

    core::Command command;
    while (app.TakeCommand(command)) {
      demo_ride.OnCommand(command, now, app);
    }

    SDL_Delay(kLoopDelayMs);
  }
}
