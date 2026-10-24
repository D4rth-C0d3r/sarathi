// The UI shell: turns knob, button and touch input into LVGL focus/clicks and screen changes,
// and keeps the screens in sync with the app model. Shared by the device and the simulator;
// their mains only feed it input and call Update().
//
//   Rotate        move focus; change a value in edit mode (slider, roller)
//   Click         activate the focused item (sent after the 300 ms double-click window)
//   Double-click  next screen
//   Long press    leave the input test screen; cancel Wi-Fi setup or a software update
//
// On the input test screen, rotation, clicks and touch go to the input test model instead.
// On the Wi-Fi setup and update screens, clicks and long presses go to those flows, rotation
// does nothing, and the screen stays awake.
//
// Settings applied here: knob direction (not on the input test, which shows the raw
// hardware), and the screen timeout: after it, the first knob step, press or touch only
// wakes the screen and is not acted on.

#pragma once

#include <cstdint>

#include "app_model.h"
#include "input_test_model.h"
#include "ota_update.h"
#include "wifi_setup.h"

namespace sarathi::ui::app_shell {

// How long the button must be held for a long press.
constexpr uint32_t kLongPressMs = 800;

// Builds the input device and screens. Call once after LVGL and a display exist. The button
// timings come from the board (board_pins.h). `random` must be a cryptographic source
// (esp_random on the device); it makes the Wi-Fi setup password.
void Init(core::AppModel& model, core::InputTestModel& input_test, uint32_t debounce_ms,
          uint32_t double_click_ms, core::OtaUpdate& ota, core::WifiSetup& wifi_setup,
          uint32_t (*random)());

// Shows a short message over any screen for `duration_ms` (for example after a rollback).
// `text` must stay valid while it shows.
void ShowNotice(const char* text, uint32_t duration_ms);

void OnKnobStep(int8_t step);
void OnButtonSample(bool raw_pressed, uint32_t now_ms);
// Returns whether LVGL should see this touch (false while the touch that woke the screen is
// held).
bool OnTouch(bool touching, int16_t x, int16_t y);

// Refreshes the screens from the model. Call after lv_timer_handler().
void Update(uint32_t now_ms);

// The backlight to apply now: the input test's own level while it is open, 0 after the screen
// timeout, otherwise the brightness setting with auto day/night (core::display_policy).
[[nodiscard]] int BacklightPercent();

// Name of the screen shown now ("nav", "music", ...), for logs.
[[nodiscard]] const char* CurrentScreenName();

}  // namespace sarathi::ui::app_shell
