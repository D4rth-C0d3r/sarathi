#include "backlight.h"

#include <Arduino.h>
#include <esp_log.h>

#include "backlight_level.h"
#include "board_pins.h"

namespace sarathi::board {
namespace {

constexpr const char* kTag = "backlight";

}  // namespace

bool Backlight::Begin(int percent) {
  if (!ledcAttach(pins::kBacklight, pins::kBacklightPwmHz, pins::kBacklightPwmBits)) {
    ESP_LOGE(kTag, "PWM attach failed on GPIO %d", pins::kBacklight);
    return false;
  }
  SetPercent(percent);
  return true;
}

void Backlight::SetPercent(int percent) {
  if (percent <= 0) {  // screen timeout: fully off
    if (percent_ != 0) {
      percent_ = 0;
      ledcWrite(pins::kBacklight, 0);
    }
    return;
  }
  const int clamped = core::ClampBacklightPercent(percent);
  if (clamped != percent_) {
    percent_ = clamped;
    ledcWrite(pins::kBacklight, core::BacklightDuty(percent_, pins::kBacklightPwmBits));
  }
}

}  // namespace sarathi::board
