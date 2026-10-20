// Adapted from Elecrow's CrowPanel 2.1" demo (vendor/crowpanel-2.1/example/Arduino/
// RotaryScreen_2_1/RotaryScreen_2_1.ino, setup()).

#include "expander.h"

#include <Arduino.h>
#include <PCF8574.h>
#include <Wire.h>
#include <esp_log.h>

#include "board_pins.h"

namespace sarathi::board::expander {
namespace {

namespace p = pins;

constexpr const char* kTag = "expander";

// NOLINTBEGIN(cppcoreguidelines-avoid-non-const-global-variables)
// The constructor only stores &Wire; the bus is started in Begin().
PCF8574 pcf(&Wire, p::kPcf8574Addr, p::kI2cSda,  // NOLINT(cppcoreguidelines-interfaces-global-init)
            p::kI2cScl);
// NOLINTEND(cppcoreguidelines-avoid-non-const-global-variables)

}  // namespace

bool Begin() {
  Wire.begin(p::kI2cSda, p::kI2cScl);

  // The library applies pin modes in begin(), so set them first.
  pcf.pinMode(p::kPcfTouchReset, OUTPUT);
  pcf.pinMode(p::kPcfTouchInt, OUTPUT);
  pcf.pinMode(p::kPcfLcdPower, OUTPUT);
  pcf.pinMode(p::kPcfLcdReset, OUTPUT);
  pcf.pinMode(p::kPcfKnobButton, INPUT_PULLUP);
  if (!pcf.begin()) {
    ESP_LOGE(kTag, "PCF8574 not responding at 0x%02X", p::kPcf8574Addr);
    return false;
  }
  return true;
}

void Write(uint8_t pin, bool high) { pcf.digitalWrite(pin, high ? HIGH : LOW); }

bool Read(uint8_t pin) { return pcf.digitalRead(pin, true) == HIGH; }

}  // namespace sarathi::board::expander
