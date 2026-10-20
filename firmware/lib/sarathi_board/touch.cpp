// Adapted from Elecrow's CrowPanel 2.1" demo (vendor/crowpanel-2.1/example/Arduino/
// RotaryScreen_2_1/RotaryScreen_2_1.ino, setup() and my_touchpad_read()).

#include "touch.h"

#include <Adafruit_CST8XX.h>
#include <Wire.h>
#include <esp_log.h>

#include "board_pins.h"

namespace sarathi::board {
namespace {

constexpr const char* kTag = "touch";

Adafruit_CST8XX controller;  // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)

}  // namespace

bool Touch::Begin() {
  if (!controller.begin(&Wire, pins::kTouchAddr)) {
    ESP_LOGE(kTag, "CST8xx not found at 0x%02X", pins::kTouchAddr);
    return false;
  }
  ESP_LOGI(kTag, "ready");
  return true;
}

bool Touch::ReadRaw(int16_t& x, int16_t& y) {
  if (controller.touched() == 0) {
    return false;
  }
  const CST_TS_Point point = controller.getPoint(0);
  x = point.x;
  y = point.y;
  return true;
}

}  // namespace sarathi::board
