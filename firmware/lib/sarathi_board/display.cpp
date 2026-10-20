// Adapted from Elecrow's CrowPanel 2.1" examples (vendor/crowpanel-2.1/example/Arduino/
// RotaryScreen_2_1/RotaryScreen_2_1.ino, setup() and the display driver block).

#include "display.h"

#include <Arduino.h>
#include <Arduino_GFX_Library.h>
#include <PCF8574.h>
#include <Wire.h>
#include <esp_log.h>

#include "board_pins.h"

namespace sarathi::board {
namespace {

namespace p = pins;

constexpr const char* kTag = "board";

// Driver objects live for the whole program and are built at static-init time, so nothing
// here is heap-allocated by us. gfx_display.begin() allocates the 480x480 RGB565 framebuffer
// in PSRAM once.
// NOLINTBEGIN(cppcoreguidelines-avoid-non-const-global-variables)
// The constructor only stores &Wire; the bus is started in Display::Begin().
PCF8574 pcf(&Wire, p::kPcf8574Addr, p::kI2cSda,  // NOLINT(cppcoreguidelines-interfaces-global-init)
            p::kI2cScl);

Arduino_SWSPI spi_bus(GFX_NOT_DEFINED /* DC: 9-bit SPI */, p::kLcdSpiCs, p::kLcdSpiSck,
                      p::kLcdSpiSda, GFX_NOT_DEFINED /* MISO */);

Arduino_ESP32RGBPanel rgb_panel(p::kLcdDe, p::kLcdVsync, p::kLcdHsync, p::kLcdPclk, p::kLcdR0,
                                p::kLcdR1, p::kLcdR2, p::kLcdR3, p::kLcdR4, p::kLcdG0, p::kLcdG1,
                                p::kLcdG2, p::kLcdG3, p::kLcdG4, p::kLcdG5, p::kLcdB0, p::kLcdB1,
                                p::kLcdB2, p::kLcdB3, p::kLcdB4, p::kHsyncPolarity,
                                p::kHsyncFrontPorch, p::kHsyncPulseWidth, p::kHsyncBackPorch,
                                p::kVsyncPolarity, p::kVsyncFrontPorch, p::kVsyncPulseWidth,
                                p::kVsyncBackPorch, p::kPclkActiveNeg, p::kPclkHz, p::kUseBigEndian,
                                p::kDeIdleHigh, p::kPclkIdleHigh, p::kBounceBufferPx);

Arduino_RGB_Display gfx_display(p::kScreenWidth, p::kScreenHeight, &rgb_panel, 0 /* rotation */,
                                true /* auto flush */, &spi_bus, GFX_NOT_DEFINED /* RST */,
                                st7701_type5_init_operations, sizeof(st7701_type5_init_operations));
// NOLINTEND(cppcoreguidelines-avoid-non-const-global-variables)

void PcfPulse(uint8_t pin, uint32_t pre_ms, uint32_t low_ms, uint32_t recover_ms) {
  pcf.digitalWrite(pin, HIGH);
  delay(pre_ms);
  pcf.digitalWrite(pin, LOW);
  delay(low_ms);
  pcf.digitalWrite(pin, HIGH);
  delay(recover_ms);
}

}  // namespace

bool Display::Begin() {
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

  pcf.digitalWrite(p::kPcfLcdPower, HIGH);
  delay(p::kLcdPowerSettleMs);
  PcfPulse(p::kPcfLcdReset, p::kLcdResetPreMs, p::kLcdResetPulseMs, p::kLcdResetRecoverMs);
  PcfPulse(p::kPcfTouchReset, p::kTouchResetPreMs, p::kTouchResetPulseMs, p::kTouchResetRecoverMs);
  pcf.digitalWrite(p::kPcfTouchInt, HIGH);
  delay(p::kTouchIntSettleMs);

  if (!gfx_display.begin()) {
    ESP_LOGE(kTag, "RGB panel init failed");
    return false;
  }
  spi_bus.beginWrite();
  spi_bus.writeCommand(p::kMadctlCmd);
  spi_bus.write(p::kMadctlValue);
  spi_bus.endWrite();
  gfx_display.fillScreen(0x0000);

  delay(p::kBacklightOnDelayMs);
  if (!ledcAttach(p::kBacklight, p::kBacklightPwmHz, p::kBacklightPwmBits)) {
    ESP_LOGE(kTag, "backlight PWM attach failed");
    return false;
  }
  ledcWrite(p::kBacklight, p::kBacklightDefaultDuty);
  // P3 LOW switches on VDD_LCD (active-low P-MOSFET), as Elecrow's demo does last.
  pcf.digitalWrite(p::kPcfLcdPower, LOW);

  ESP_LOGI(kTag, "display ready (%dx%d)", p::kScreenWidth, p::kScreenHeight);
  return true;
}

void Display::Fill(uint16_t rgb565) { gfx_display.fillScreen(ToPanel(rgb565)); }

void Display::DrawRing(int16_t cx, int16_t cy, int16_t outer_radius, int16_t thickness,
                       uint16_t rgb565) {
  gfx_display.fillArc(cx, cy, outer_radius, static_cast<int16_t>(outer_radius - thickness), 0.0F,
                      360.0F, ToPanel(rgb565));
}

void Display::DrawCentredText(const char* text, int16_t cy, uint8_t scale, uint16_t rgb565) {
  int16_t x1 = 0;
  int16_t y1 = 0;
  uint16_t w = 0;
  uint16_t h = 0;
  gfx_display.setTextSize(scale);
  gfx_display.getTextBounds(text, 0, 0, &x1, &y1, &w, &h);
  gfx_display.setCursor(static_cast<int16_t>((p::kScreenWidth - w) / 2 - x1),
                        static_cast<int16_t>(cy - (h / 2) - y1));
  gfx_display.setTextColor(ToPanel(rgb565));
  gfx_display.print(text);
}

uint16_t Display::ToPanel(uint16_t rgb565) {
  if constexpr (p::kSwapRedBlue) {
    return static_cast<uint16_t>((rgb565 & 0x07E0U) | ((rgb565 & 0x001FU) << 11U) |
                                 ((rgb565 & 0xF800U) >> 11U));
  }
  return rgb565;
}

Arduino_GFX& Display::Gfx() { return gfx_display; }

}  // namespace sarathi::board
