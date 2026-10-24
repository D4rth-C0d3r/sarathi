// Diagnostic A/B test (ADR-0004 step 7): HTTPS connection reliability with the RGB panel off
// and on. Uses the Wi-Fi network saved on the device (Settings -> Wi-Fi), so no credentials
// are needed at build time. Run: pio test -e crowpanel21 -f test_device_net -v
// The test firmware replaces the app; flash the app again afterwards.

#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include <Arduino.h>
#include <esp_heap_caps.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <unity.h>

#include "backlight.h"
#include "display.h"
#include "https_get.h"
#include "nvs_wifi.h"
#include "wifi_link.h"

namespace ota = sarathi::ota;

// Unity calls these by name, so they need external linkage and Unity's spelling.
void setUp() {}     // NOLINT(misc-use-internal-linkage,readability-identifier-naming)
void tearDown() {}  // NOLINT(misc-use-internal-linkage,readability-identifier-naming)

namespace {

// Two hosts, as an update uses: github.com redirects to raw.githubusercontent.com.
constexpr const char* kUrl =
    "https://github.com/D4rth-C0d3r/sarathi/raw/1d1ac7d/docs/ota/vectors/valid.jws";
// What an update fetches: a release asset, redirected to a long signed URL on another host.
constexpr const char* kReleaseUrl =
    "https://github.com/D4rth-C0d3r/sarathi/releases/download/v0.3.0-rc.1/manifest.jws";
constexpr int kRounds = 8;
constexpr int16_t kSize = 480;
constexpr int16_t kStripe = 40;

void Log(const char* format, ...) {
  char line[200];
  va_list args;
  va_start(args, format);
  std::vsnprintf(line, sizeof(line), format, args);
  va_end(args);
  TEST_MESSAGE(line);
}

bool Discard(void* /*context*/, const uint8_t* /*data*/, size_t /*size*/) { return true; }

// Like the app: the panel scans out of PSRAM all the time, and the UI keeps drawing.
void DrawLoad(void* arg) {
  auto* display = static_cast<sarathi::board::Display*>(arg);
  auto* stripe = static_cast<uint16_t*>(
      heap_caps_malloc(kSize * kStripe * sizeof(uint16_t), MALLOC_CAP_SPIRAM));
  uint16_t colour = 0;
  for (;;) {
    for (int i = 0; i < kSize * kStripe; ++i) {
      stripe[i] = colour;  // NOLINT(*-pointer-arithmetic)
    }
    for (int16_t y = 0; y < kSize; y += kStripe) {
      display->DrawBitmap(0, y, kSize, kStripe, stripe);
    }
    colour += 0x0841;
    vTaskDelay(pdMS_TO_TICKS(30));  // about the app's frame rate on Nav
  }
}

int Rounds(const char* label, const char* url = kUrl) {
  int ok = 0;
  int64_t total_ms = 0;
  for (int i = 0; i < kRounds; ++i) {
    ota::FetchReport report;
    const int64_t start = esp_timer_get_time();
    const ota::FetchResult result = ota::HttpsGet(url, 4096, Discard, nullptr, &report);
    const int64_t ms = (esp_timer_get_time() - start) / 1000;
    total_ms += ms;
    ok += result == ota::FetchResult::kOk ? 1 : 0;
    Log("%s round %d: %s, HTTP %d, %d redirect(s), %lld ms, heap %u KB (min %u)", label, i + 1,
        ota::FetchResultName(result), report.status, report.redirects, ms,
        static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024),
        static_cast<unsigned>(heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL) / 1024));
  }
  Log("%s: %d/%d fetches succeeded, %lld ms in all", label, ok, kRounds, total_ms);
  return ok;
}

sarathi::settings::NvsWifi nvs_wifi;  // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)
sarathi::core::WifiCredentials
    credentials;  // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)

void TestDisplayOff() {
  TEST_ASSERT_TRUE_MESSAGE(nvs_wifi.Begin() && nvs_wifi.Load(&credentials),
                           "no Wi-Fi saved: set it up in Settings first");
  TEST_ASSERT_TRUE(ota::WifiConnect(credentials));
  Rounds("release asset, display off", kReleaseUrl);
  ota::WifiOff();
}

void TestDisplayOn() {
  static sarathi::board::Backlight backlight;
  static sarathi::board::Display display;
  TEST_ASSERT_TRUE(display.Begin(backlight, 50));
  xTaskCreatePinnedToCore(DrawLoad, "draw", 4096, &display, 1, nullptr, 1);
  vTaskDelay(pdMS_TO_TICKS(500));
  TEST_ASSERT_TRUE(ota::WifiConnect(credentials));
  Rounds("display on");
  ota::WifiOff();
}

// Holds internal RAM until about `free_kb` is left, like the running app (LVGL, the UI and the
// OTA worker leave about 110 KB before Wi-Fi starts).
void HogInternal(size_t free_kb) {
  constexpr size_t kBlock = 4096;
  while (heap_caps_get_free_size(MALLOC_CAP_INTERNAL) > free_kb * 1024 + kBlock) {
    if (heap_caps_malloc(kBlock, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT) == nullptr) {
      break;
    }
  }
  Log("internal heap now %u KB free",
      static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024));
}

void TestDisplayOnLowMemory() {
  HogInternal(110);
  TEST_ASSERT_TRUE(ota::WifiConnect(credentials));
  Rounds("display on, app-level RAM");
  ota::WifiOff();
}

}  // namespace

void setup() {
  delay(2000);  // let the USB serial port come up so the runner sees every line
  UNITY_BEGIN();
  RUN_TEST(TestDisplayOff);
  // The raw-file rounds above already passed 24/24 (display off, on, and on with app-level RAM).
  UNITY_END();
}

void loop() {}
