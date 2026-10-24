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
#include <esp_random.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "app_model.h"
#include "app_shell.h"
#include "backlight.h"
#include "board_pins.h"
#include "boot_confirm.h"
#include "boot_health.h"
#include "demo_ride.h"
#include "display.h"
#include "frame_stats.h"
#include "input_test_model.h"
#include "knob.h"
#include "lvgl_port.h"
#include "nvs_settings.h"
#include "nvs_wifi.h"
#include "ota_update.h"
#include "ota_worker.h"
#include "settings_saver.h"
#include "touch.h"
#include "touch_filter.h"
#include "version.h"
#include "wifi_setup.h"
#include "wifi_setup_server.h"

namespace {

namespace board = sarathi::board;
namespace core = sarathi::core;
namespace ota = sarathi::ota;
namespace ui = sarathi::ui;
namespace pins = sarathi::board::pins;

constexpr const char* kTag = "main";
constexpr uint32_t kLogPeriodMs = 1000;
constexpr uint32_t kBootLogWindowMs = 10000;
constexpr uint32_t kPerfPeriodMs = 5000;
constexpr uint32_t kSerialWaitMs = 2000;
constexpr uint32_t kLoopDelayMs = 5;
constexpr uint32_t kKiB = 1024;
constexpr uint32_t kMiB = 1024 * 1024;
constexpr uint32_t kNoticeMs = 8000;
constexpr const char* kRollbackMarker = "rb_seen";  // slot address of the last notice shown

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
sarathi::settings::NvsSettings nvs;
core::SettingsSaver settings_saver;
bool settings_found = false;  // the boot load found saved settings
ota::BootState boot_state;    // read at boot: is this a fresh OTA image awaiting confirmation?
bool first_frame = false;     // a frame has reached the panel
core::OtaUpdate ota_update;   // lives on the UI task; the worker runs its jobs
ota::OtaWorker ota_worker;
core::WifiSetup wifi_setup;
ota::WifiSetupServer setup_server;
sarathi::settings::NvsWifi nvs_wifi;
// NOLINTEND(cppcoreguidelines-avoid-non-const-global-variables)

uint32_t TimeUs() { return static_cast<uint32_t>(esp_timer_get_time()); }

uint32_t Random() { return esp_random(); }

// Runs on the OTA worker task. NVS is safe to read from there.
bool LoadWifi(core::WifiCredentials* out) { return nvs_wifi.Load(out); }

void FlushToDisplay(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t* pixels) {
  const uint32_t start = TimeUs();
  display.DrawBitmap(x, y, w, h, pixels);
  flush_stats.Add(TimeUs() - start);
  first_frame = true;
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
  return ui::app_shell::OnTouch(true, p.x, p.y);  // false: this touch only wakes the screen
}

void LoadSettings() {
  if (nvs.Begin()) {
    app.LoadSettings(nvs.Load(&settings_found));
  }
  settings_saver.MarkSaved(app.SettingsRevision());
}

// Repeated with the hello line for the first seconds: native USB CDC comes up after setup(),
// so a line logged only once at boot never reaches the monitor.
void LogBootSettings() {
  const core::Settings& s = app.State().settings;
  ESP_LOGI(kTag,
           "settings %s: theme %u, brightness %d%%, day/night %s, units %s, screen off %u, knob %s",
           settings_found ? "loaded" : "defaults", static_cast<unsigned>(s.theme),
           s.brightness_percent, s.auto_day_night ? "auto" : "off",
           s.units == core::Units::kMetric ? "metric" : "imperial",
           static_cast<unsigned>(s.screen_timeout), s.knob_reversed ? "reversed" : "normal");
  ESP_LOGI(kTag, "firmware %.*s (seq %u), image %s%s",
           static_cast<int>(core::kFirmwareVersion.size()), core::kFirmwareVersion.data(),
           static_cast<unsigned>(core::kFirmwareSeq),
           boot_state.pending_verify ? "pending verify" : "not pending",
           boot_state.rolled_back ? ", an earlier update was rolled back" : "");
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

// Wi-Fi setup and OTA: hand jobs to the platform, feed results back (ADR-0004).
void RunOta(uint32_t now) {
  ota_worker.Post(ota_update.TakeJob());
  ota::OtaResult result;
  while (ota_worker.TakeResult(&result)) {
    ota::ApplyResult(result, ota_update);
  }

  switch (wifi_setup.TakeJob()) {
    case core::WifiSetupJob::kStartAccessPoint:
      if (!setup_server.Start(wifi_setup.Password())) {
        wifi_setup.OnAccessPointStarted(false);
      }  // otherwise Poll() reports it once the scan is done and the AP is up
      break;
    case core::WifiSetupJob::kStopAccessPoint:
      setup_server.Stop();
      break;
    case core::WifiSetupJob::kForget:
      nvs_wifi.Erase();
      wifi_setup.SetSaved("");
      break;
    case core::WifiSetupJob::kNone:
      break;
  }
  if (wifi_setup.Active()) {
    const ota::WifiSetupServer::Events events = setup_server.Poll(now, wifi_setup);
    if (events.ap_done) {
      wifi_setup.OnAccessPointStarted(events.ap_ok);
    }
    if (events.submitted) {
      wifi_setup.OnSubmitted(events.credentials.ssid.View());
    }
    if (events.join_done) {
      // Saved only once the network is known to work.
      wifi_setup.OnJoinResult(events.join_ok && nvs_wifi.Save(events.credentials));
    }
  }
  wifi_setup.Tick(now);
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
  RunOta(now);
  // Debounced: one write once the settings have been still for 2 s.
  if (settings_saver.ShouldSave(app.SettingsRevision(), now) && !nvs.Save(app.State().settings)) {
    settings_saver.SaveFailed(now);
  }
  backlight.SetPercent(ui::app_shell::BacklightPercent());

  // A fresh OTA image confirms itself once the UI has run for a while (ADR-0004 §5); a reset
  // before that rolls back to the previous image.
  if (boot_state.pending_verify &&
      core::DecideBoot({true, first_frame, false, now}) == core::BootDecision::kConfirm) {
    boot_state.pending_verify = !ota::ConfirmBoot();
    if (!boot_state.pending_verify) {
      nvs.SaveMarker(kRollbackMarker, 0);  // a later rollback deserves a notice again
    }
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

  boot_state = ota::ReadBootState();
  LoadSettings();  // before the display, so the saved brightness and theme apply from the start
  if (!display.Begin(backlight, app.State().settings.brightness_percent)) {
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
  if (nvs_wifi.Begin()) {
    core::WifiCredentials saved;
    if (nvs_wifi.Load(&saved)) {
      wifi_setup.SetSaved(saved.ssid.View());
    }
  }
  if (!ota_worker.Begin(LoadWifi)) {
    ESP_LOGE(kTag, "OTA worker failed to start");
  }
  ui::app_shell::Init(app, input_test, pins::kKnobDebounceMs, pins::kKnobDoubleClickMs, ota_update,
                      wifi_setup, Random);
  if (boot_state.rolled_back && nvs.LoadMarker(kRollbackMarker) != boot_state.invalid_address) {
    ui::app_shell::ShowNotice("Update failed. The previous version was restored.", kNoticeMs);
    nvs.SaveMarker(kRollbackMarker, boot_state.invalid_address);
  }
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
    if (now < kBootLogWindowMs) {
      LogBootSettings();
    }
  }
  if (ui_ready && now - last_perf_ms >= kPerfPeriodMs) {
    last_perf_ms = now;
    LogPerf();
  }
  delay(kLoopDelayMs);
}
