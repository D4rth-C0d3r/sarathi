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
  percent_ = core::ClampBacklightPercent(percent);
  ledcWrite(pins::kBacklight, core::BacklightDuty(percent_, pins::kBacklightPwmBits));
}

}  // namespace sarathi::board
