// Adapted from Elecrow's CrowPanel 2.1" examples (vendor/crowpanel-2.1/example/Arduino/
// RotaryScreen_2_1/RotaryScreen_2_1.ino, setup(), the display driver block and
// my_disp_flush()).

#include "display.h"

#include <cstddef>

#include <Arduino.h>
#include <Arduino_GFX_Library.h>
#include <esp_log.h>

#include "backlight.h"
#include "board_pins.h"
#include "expander.h"
#include "rgb565.h"

namespace sarathi::board {
namespace {

namespace p = pins;

constexpr const char* kTag = "display";

// Driver objects live for the whole program and are built at static-init time, so nothing
// here is heap-allocated by us. gfx_display.begin() allocates the 480x480 RGB565 framebuffer
// in PSRAM once.
// NOLINTBEGIN(cppcoreguidelines-avoid-non-const-global-variables)
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

void ExpanderPulse(uint8_t pin, uint32_t pre_ms, uint32_t low_ms, uint32_t recover_ms) {
  expander::Write(pin, true);
  delay(pre_ms);
  expander::Write(pin, false);
  delay(low_ms);
  expander::Write(pin, true);
  delay(recover_ms);
}

}  // namespace

bool Display::Begin(Backlight& backlight, int backlight_percent) {
  if (!expander::Begin()) {
    return false;
  }

  expander::Write(p::kPcfLcdPower, true);
  delay(p::kLcdPowerSettleMs);
  ExpanderPulse(p::kPcfLcdReset, p::kLcdResetPreMs, p::kLcdResetPulseMs, p::kLcdResetRecoverMs);
  ExpanderPulse(p::kPcfTouchReset, p::kTouchResetPreMs, p::kTouchResetPulseMs,
                p::kTouchResetRecoverMs);
  expander::Write(p::kPcfTouchInt, true);
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
  if (!backlight.Begin(backlight_percent)) {
    return false;
  }
  // P3 LOW switches on VDD_LCD (active-low P-MOSFET), as Elecrow's demo does last.
  expander::Write(p::kPcfLcdPower, false);

  ESP_LOGI(kTag, "ready (%dx%d)", p::kScreenWidth, p::kScreenHeight);
  return true;
}

void Display::DrawBitmap(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t* pixels) {
  if constexpr (p::kSwapRedBlue) {
    const size_t count = static_cast<size_t>(w) * static_cast<size_t>(h);
    for (size_t i = 0; i < count; ++i) {
      pixels[i] = core::SwapRedBlue(pixels[i]);  // NOLINT(*-pointer-arithmetic)
    }
  }
  gfx_display.draw16bitRGBBitmap(x, y, pixels, w, h);
}

}  // namespace sarathi::board
