// sārathi firmware entry point: constructs the modules and wires them together.
//
// Phase 1: the UI shell (Nav, Music, Settings, Call, Input test) fed by the scripted demo
// ride until the phone link exists. Also logs chip, flash and PSRAM info once a second.

#include <cinttypes>
#include <cstdint>

#include <Arduino.h>
#include <esp_flash.h>
#include <esp_heap_caps.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "app_model.h"
#include "app_shell.h"
#include "backlight.h"
#include "board_pins.h"
#include "demo_ride.h"
#include "display.h"
#include "frame_stats.h"
#include "input_test_model.h"
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
constexpr uint32_t kPerfPeriodMs = 5000;
constexpr uint32_t kSerialWaitMs = 2000;
constexpr uint32_t kLoopDelayMs = 5;
constexpr uint32_t kKiB = 1024;
constexpr uint32_t kMiB = 1024 * 1024;

// NOLINTBEGIN(cppcoreguidelines-avoid-non-const-global-variables)
board::Backlight backlight;
board::Display display;
board::Touch touch;
board::Knob knob;
core::TouchFilter touch_filter(pins::kScreenWidth, pins::kScreenHeight, pins::kTouchYOffset,
                               pins::kTouchJitterPx);
core::AppModel app;
core::DemoRide demo_ride;
core::InputTestModel input_test(pins::kKnobDebounceMs, pins::kKnobDoubleClickMs,
                                pins::kBacklightDefaultPercent, core::kBacklightStepPercent);
bool ui_ready = false;
bool touch_ready = false;
uint32_t last_log_ms = 0;
uint32_t last_perf_ms = 0;
core::FrameStats flush_stats;
// NOLINTEND(cppcoreguidelines-avoid-non-const-global-variables)

uint32_t TimeUs() { return static_cast<uint32_t>(esp_timer_get_time()); }

void FlushToDisplay(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t* pixels) {
  const uint32_t start = TimeUs();
  display.DrawBitmap(x, y, w, h, pixels);
  flush_stats.Add(TimeUs() - start);
}

bool ReadTouch(int16_t& x, int16_t& y) {
  int16_t raw_x = 0;
  int16_t raw_y = 0;
  if (!touch_ready || !touch.ReadRaw(raw_x, raw_y)) {
    touch_filter.Release();
    ui::app_shell::OnTouch(false, 0, 0);
    return false;
  }
  const core::TouchPoint p = touch_filter.Apply(raw_x, raw_y);
  x = p.x;
  y = p.y;
  ui::app_shell::OnTouch(true, p.x, p.y);
  return true;
}

uint32_t TickMs() { return millis(); }

void* AllocPsram(size_t bytes) { return heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM); }

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

// Frame time and memory, every kPerfPeriodMs (ADR-0002 rendering measurement).
void LogPerf() {
  const core::FrameStats::Summary frames = ui::lvgl_port::TakeFrameStats();
  const core::FrameStats::Summary flushes = flush_stats.TakeSummary();
  constexpr float kMs = 1000.0F;
#ifdef SARATHI_NAV_STRESS
  constexpr const char* kBuild = "stress";
#else
  constexpr const char* kBuild = "normal";
#endif
  ESP_LOGI(kTag,
           "perf %s %s | frames %u/%us | frame avg %.1f ms max %.1f ms | flush avg %.1f ms max "
           "%.1f ms (%u) | heap int %u KB free (min %u, largest %u) | psram %u KB free (min "
           "%u) | stack free ui %u B, knob %u B",
           kBuild, ui::app_shell::CurrentScreenName(), static_cast<unsigned>(frames.count),
           static_cast<unsigned>(kPerfPeriodMs / 1000),
           static_cast<double>(static_cast<float>(frames.avg_us) / kMs),
           static_cast<double>(static_cast<float>(frames.max_us) / kMs),
           static_cast<double>(static_cast<float>(flushes.avg_us) / kMs),
           static_cast<double>(static_cast<float>(flushes.max_us) / kMs),
           static_cast<unsigned>(flushes.count),
           static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / kKiB),
           static_cast<unsigned>(heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL) / kKiB),
           static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL) / kKiB),
           static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / kKiB),
           static_cast<unsigned>(heap_caps_get_minimum_free_size(MALLOC_CAP_SPIRAM) / kKiB),
           static_cast<unsigned>(uxTaskGetStackHighWaterMark(nullptr)),
           static_cast<unsigned>(knob.StackFreeBytes()));
}

void RunUi(uint32_t now) {
  demo_ride.Poll(now, app);

  int8_t step = 0;
  while (knob.TakeStep(step)) {
    ui::app_shell::OnKnobStep(step);
  }
  ui::app_shell::OnButtonSample(knob.ReadButtonRaw(), now);

  ui::lvgl_port::Run();  // reads touch via ReadTouch()
  ui::app_shell::Update(now);

  core::Command command;
  while (app.TakeCommand(command)) {
    demo_ride.OnCommand(command, now, app);
  }
  backlight.SetPercent(ui::app_shell::BacklightPercent());
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
  const auto buffer_bytes =
      static_cast<unsigned>(ui::lvgl_port::BufferBytes(pins::kScreenWidth, pins::kScreenHeight));
  if (!ui::lvgl_port::Init(pins::kScreenWidth, pins::kScreenHeight, FlushToDisplay, ReadTouch,
                           TickMs, AllocPsram)) {
    ESP_LOGE(kTag, "LVGL draw buffer alloc failed (2 x %u B in PSRAM); UI disabled", buffer_bytes);
    return;
  }
  ESP_LOGI(kTag, "LVGL ready (2 x %u B buffers in PSRAM)", buffer_bytes);
  ui::lvgl_port::AttachFrameTimer(lv_display_get_default(), TimeUs);
  ui::app_shell::Init(app, input_test, pins::kKnobDebounceMs, pins::kKnobDoubleClickMs);
  ui_ready = true;
}

void loop() {
  const uint32_t now = millis();
  if (ui_ready) {
    RunUi(now);
  }
  if (now - last_log_ms >= kLogPeriodMs) {
    last_log_ms = now;
    LogHello();
  }
  if (ui_ready && now - last_perf_ms >= kPerfPeriodMs) {
    last_perf_ms = now;
    LogPerf();
  }
  delay(kLoopDelayMs);
}
