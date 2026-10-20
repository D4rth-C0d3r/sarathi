// sārathi firmware entry point.
//
// Phase 0 bring-up: starts the display and runs the colour/ring check, then logs chip, flash
// and PSRAM info once a second, to confirm the board configuration (16 MB flash, OPI PSRAM,
// 240 MHz) on real hardware. This is a temporary exception to the "main.cpp only wires
// modules" rule and is replaced in Phase 1.

#include <cinttypes>
#include <cstdint>

#include <Arduino.h>
#include <esp_flash.h>
#include <esp_log.h>

#include "bringup_pattern.h"
#include "display.h"

namespace {

constexpr const char* kTag = "main";
constexpr uint32_t kLogPeriodMs = 1000;
constexpr uint32_t kSerialWaitMs = 2000;
constexpr uint32_t kKiB = 1024;
constexpr uint32_t kMiB = 1024 * 1024;

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

}  // namespace

void setup() {
  Serial.begin(115200);
  Serial.setDebugOutput(true);  // route ESP_LOGx to the native USB CDC port
  const uint32_t start = millis();
  while (!Serial && millis() - start < kSerialWaitMs) {
    delay(10);
  }

  sarathi::board::Display display;
  if (display.Begin()) {
    sarathi::board::RunBringUpPattern(display);
  } else {
    ESP_LOGE(kTag, "display init failed; skipping bring-up pattern");
  }
}

void loop() {
  LogHello();
  delay(kLogPeriodMs);
}
