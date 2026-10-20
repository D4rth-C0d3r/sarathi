// sārathi firmware entry point.
//
// Phase 0 bring-up: starts the display, touch, knob and LVGL, and shows the input test
// screen (knob count, button, touch position, backlight). The knob also sets the backlight.
// Logs chip, flash and PSRAM info once a second. This is a temporary exception to the
// "main.cpp only wires modules" rule and is replaced in Phase 1.

#include <cinttypes>
#include <cstdint>

#include <Arduino.h>
#include <esp_flash.h>
#include <esp_log.h>

#include "backlight.h"
#include "board_pins.h"
#include "button_debouncer.h"
#include "display.h"
#include "input_test_screen.h"
#include "knob.h"
#include "lvgl_port.h"
#include "touch.h"
#include "touch_filter.h"

namespace {

namespace board = sarathi::board;
namespace core = sarathi::core;
namespace ui = sarathi::ui;
namespace pins = sarathi::board::pins;

constexpr const char* kTag = "main";
constexpr uint32_t kLogPeriodMs = 1000;
constexpr uint32_t kSerialWaitMs = 2000;
constexpr uint32_t kLoopDelayMs = 5;
constexpr int kBacklightStepPercent = 5;  // per detent, as in Elecrow's Lesson02-Rotate
constexpr uint32_t kKiB = 1024;
constexpr uint32_t kMiB = 1024 * 1024;

// NOLINTBEGIN(cppcoreguidelines-avoid-non-const-global-variables)
board::Backlight backlight;
board::Display display;
board::Touch touch;
board::Knob knob;
core::TouchFilter touch_filter(pins::kScreenWidth, pins::kScreenHeight, pins::kTouchYOffset,
                               pins::kTouchJitterPx);
core::ButtonDebouncer button(pins::kKnobDebounceMs, pins::kKnobDoubleClickMs);
ui::input_test_screen::State screen_state;
bool ui_ready = false;
bool touch_ready = false;
uint32_t last_log_ms = 0;
// NOLINTEND(cppcoreguidelines-avoid-non-const-global-variables)

void FlushToDisplay(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t* pixels) {
  display.DrawBitmap(x, y, w, h, pixels);
}

bool ReadTouch(int16_t& x, int16_t& y) {
  int16_t raw_x = 0;
  int16_t raw_y = 0;
  if (!touch_ready || !touch.ReadRaw(raw_x, raw_y)) {
    touch_filter.Release();
    screen_state.touching = false;
    return false;
  }
  const core::TouchPoint p = touch_filter.Apply(raw_x, raw_y);
  x = p.x;
  y = p.y;
  screen_state.touching = true;
  screen_state.touch_x = p.x;
  screen_state.touch_y = p.y;
  return true;
}

void LogHello() {
  uint32_t flash_physical = 0;
  if (esp_flash_get_physical_size(esp_flash_default_chip, &flash_physical) != ESP_OK) {
    flash_physical = 0;
  }
  const uint16_t rev = ESP.getChipRevision();  // major * 100 + minor

  ESP_LOGI(kTag,
           "hello from sarathi | %s v%u.%u, %u cores @ %" PRIu32 " MHz | flash %" PRIu32
           " MB (chip %" PRIu32 " MB) | PSRAM %" PRIu32 " KB (%" PRIu32 " KB free) | heap %" PRIu32
           " KB free | up %" PRIu32 " s",
           ESP.getChipModel(), rev / 100, rev % 100, ESP.getChipCores(), ESP.getCpuFreqMHz(),
           ESP.getFlashChipSize() / kMiB, flash_physical / kMiB, ESP.getPsramSize() / kKiB,
           ESP.getFreePsram() / kKiB, ESP.getFreeHeap() / kKiB, millis() / 1000);
}

void PollInputs() {
  int8_t step = 0;
  while (knob.TakeStep(step)) {
    screen_state.knob_count += step;
    backlight.SetPercent(backlight.Percent() + (step * kBacklightStepPercent));
  }
  screen_state.backlight_percent = backlight.Percent();

  const core::ButtonUpdate b = button.Update(knob.ReadButtonRaw(), millis());
  screen_state.button_pressed = b.pressed;
  if (b.gesture != core::ButtonGesture::kNone) {
    screen_state.last_gesture = b.gesture;
  }
}

}  // namespace

void setup() {
  Serial.begin(115200);
  Serial.setDebugOutput(true);  // route ESP_LOGx to the native USB CDC port
  const uint32_t start = millis();
  while (!Serial && millis() - start < kSerialWaitMs) {
    delay(10);
  }

  if (!display.Begin(backlight, pins::kBacklightDefaultPercent)) {
    ESP_LOGE(kTag, "display init failed; UI disabled");
    return;
  }
  touch_ready = touch.Begin();
  if (!knob.Begin()) {
    ESP_LOGE(kTag, "knob init failed");
  }
  if (!ui::lvgl_port::Init(pins::kScreenWidth, pins::kScreenHeight, FlushToDisplay, ReadTouch)) {
    ESP_LOGE(kTag, "LVGL init failed; UI disabled");
    return;
  }
  ui::input_test_screen::Create();
  ui_ready = true;
}

void loop() {
  if (ui_ready) {
    PollInputs();
    ui::lvgl_port::Run();  // reads touch via ReadTouch()
    ui::input_test_screen::Update(screen_state);
  }

  const uint32_t now = millis();
  if (now - last_log_ms >= kLogPeriodMs) {
    last_log_ms = now;
    LogHello();
  }
  delay(kLoopDelayMs);
}
