#include "bringup_pattern.h"

#include <array>
#include <cstdint>

#include <Arduino.h>
#include <esp_log.h>

#include "board_pins.h"
#include "display.h"

namespace sarathi::board {
namespace {

constexpr const char* kTag = "bringup";

constexpr uint16_t kBlack = 0x0000;
constexpr uint16_t kRed = 0xF800;
constexpr uint16_t kGreen = 0x07E0;
constexpr uint16_t kBlue = 0x001F;
constexpr uint16_t kWhite = 0xFFFF;

constexpr uint32_t kSwatchMs = 2000;
constexpr uint8_t kLabelScale = 8;  // built-in 6x8 font -> 48x64 px per character
constexpr int16_t kRingThickness = 4;

struct Swatch {
  uint16_t fill;
  uint16_t label;
  const char* name;
};

constexpr std::array<Swatch, 4> kSwatches = {{
    {kRed, kWhite, "RED"},
    {kGreen, kBlack, "GREEN"},
    {kBlue, kWhite, "BLUE"},
    {kWhite, kBlack, "WHITE"},
}};

}  // namespace

void RunBringUpPattern(Display& display) {
  constexpr int16_t kCx = pins::kScreenWidth / 2;
  constexpr int16_t kCy = pins::kScreenHeight / 2;

  for (const Swatch& s : kSwatches) {
    ESP_LOGI(kTag, "showing %s", s.name);
    display.Fill(s.fill);
    display.DrawCentredText(s.name, kCy, kLabelScale, s.label);
    delay(kSwatchMs);
  }

  ESP_LOGI(kTag, "showing ring");
  display.Fill(kBlack);
  display.DrawRing(kCx, kCy, kCx - 1, kRingThickness, kWhite);
}

}  // namespace sarathi::board
