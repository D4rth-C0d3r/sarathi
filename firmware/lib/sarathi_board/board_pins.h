// Pins, I2C addresses and panel timings for the Elecrow CrowPanel 2.1" Rotary Display
// (DHE03921D). This is the only file that holds these values (see CLAUDE.md).
//
// Sources, all under vendor/crowpanel-2.1/ unless marked otherwise:
//   [demo]   example/Arduino/RotaryScreen_2_1/RotaryScreen_2_1.ino   (Elecrow's LVGL demo)
//   [lesson] example/Arduino/Examples/Lesson02-Rotate/Board21.h      (same in Lessons 02-05)
//   [sch]    Eagle_SCH&PCB/ESP32_Display_2.1(K)_Main_V1.0.sch        (net names)
//   [wiki]   https://www.elecrow.com/wiki/2.1_Arduino_LVGL_Rotary_Guide.html
// Cross-checked against all four in October 2026. See docs/hardware.md for discrepancies.

#pragma once

#include <cstddef>
#include <cstdint>

namespace sarathi::board::pins {

// ---- I2C bus (PCF8574 + touch controller) ----
constexpr int kI2cSda = 38;  // [demo] L14, [lesson] L16, [sch] net GPIO38-SDA, [wiki]
constexpr int kI2cScl = 39;  // [demo] L15, [lesson] L17, [sch] net GPIO39-SCL, [wiki]

// ---- PCF8574 I/O expander ----
// [demo] L17, [lesson] L27, [wiki]. [sch] U3 straps: A0 -> 3.3V, A1/A2 -> GND => 0x20 | 1.
constexpr uint8_t kPcf8574Addr = 0x21;
// Expander pin indices, [demo] L471-475, [sch] U3 nets.
constexpr uint8_t kPcfTouchReset = 0;  // P0, net TP_RST
constexpr uint8_t kPcfTouchInt = 2;    // P2, net TP_INT (Elecrow drives it HIGH as an output)
// P3, net LED-ON: gate of P-MOSFET Q1 (3.3V -> VDD_LCD) with a 10k pull-up, so LOW powers
// VDD_LCD. [demo] drives it HIGH through reset and init, then LOW after the backlight (L579).
constexpr uint8_t kPcfLcdPower = 3;
constexpr uint8_t kPcfLcdReset = 4;    // P4, net LCD_RST
constexpr uint8_t kPcfKnobButton = 5;  // P5, net ENCODER_SW, input with pull-up

// ---- ST7701S init bus: 3-wire SPI, 9-bit (no DC pin) ----
// [demo] L88-90, [lesson] L29-30, [sch] nets LCD_CS (GPIO16/XTAL_32K_N), LCD_SCK, LCD_SDA.
constexpr int kLcdSpiCs = 16;
constexpr int kLcdSpiSck = 2;
constexpr int kLcdSpiSda = 1;

// ---- RGB parallel bus ----
// [demo] L93-96, [lesson] L32-33, [wiki]. [sch] numbers the colour lines from 1 (R1..R5),
// the GFX constructor from 0 (R0..R4); the GPIOs are identical.
constexpr int kLcdDe = 40;
constexpr int kLcdVsync = 7;
constexpr int kLcdHsync = 15;  // [sch] GPIO15/XTAL_32K_P
constexpr int kLcdPclk = 41;
constexpr int kLcdR0 = 46;
constexpr int kLcdR1 = 3;
constexpr int kLcdR2 = 8;
constexpr int kLcdR3 = 18;
constexpr int kLcdR4 = 17;
constexpr int kLcdG0 = 14;
constexpr int kLcdG1 = 13;
constexpr int kLcdG2 = 12;
constexpr int kLcdG3 = 11;
constexpr int kLcdG4 = 10;
constexpr int kLcdG5 = 9;
constexpr int kLcdB0 = 5;
constexpr int kLcdB1 = 45;
constexpr int kLcdB2 = 48;
constexpr int kLcdB3 = 47;
constexpr int kLcdB4 = 21;

// ---- Panel timing ----
// [demo] L97-101, [lesson] L33-34, [wiki] ("12 MHz pixel clock", "10/4/20", "480 * 20").
constexpr uint16_t kHsyncPolarity = 1;
constexpr uint16_t kHsyncFrontPorch = 10;
constexpr uint16_t kHsyncPulseWidth = 4;
constexpr uint16_t kHsyncBackPorch = 20;
constexpr uint16_t kVsyncPolarity = 1;
constexpr uint16_t kVsyncFrontPorch = 10;
constexpr uint16_t kVsyncPulseWidth = 4;
constexpr uint16_t kVsyncBackPorch = 20;
constexpr uint16_t kPclkActiveNeg = 0;
constexpr int32_t kPclkHz = 12'000'000;
constexpr bool kUseBigEndian = false;
constexpr uint16_t kDeIdleHigh = 0;
constexpr uint16_t kPclkIdleHigh = 0;
constexpr std::size_t kBounceBufferPx = std::size_t{480} * 20;

// ---- Panel geometry and colour ----
constexpr int16_t kScreenWidth = 480;   // [demo] L104, [lesson] L14
constexpr int16_t kScreenHeight = 480;  // [demo] L104, [lesson] L15
// MADCTL (0x36) = 0x08 after gfx->begin(). [demo] L510-513, [lesson] L93-96, [wiki].
constexpr uint8_t kMadctlCmd = 0x36;
constexpr uint8_t kMadctlValue = 0x08;
// Swap the R and B fields of RGB565 before drawing. [demo] L126-139, [wiki]. Elecrow's lessons
// don't swap; the demo says ESP-IDF 5's RGB path needs it. Verified on hardware, Oct 2026.
constexpr bool kSwapRedBlue = true;

// ---- Backlight ----
// [demo] L39-42 and L429-430, [lesson] L18 and L76-78, [sch] net BL_PWM -> NPN Q3 low side.
constexpr int kBacklight = 6;
constexpr uint32_t kBacklightPwmHz = 5000;
constexpr uint8_t kBacklightPwmBits = 8;
constexpr uint32_t kBacklightDefaultDuty = 204;  // ~80 %

// ---- Power-up delays, ms ([demo] L484-505 and L577; longer than [lesson] L63-72) ----
constexpr uint32_t kLcdPowerSettleMs = 100;     // after P3 HIGH
constexpr uint32_t kLcdResetPreMs = 100;        // P4 HIGH
constexpr uint32_t kLcdResetPulseMs = 120;      // P4 LOW
constexpr uint32_t kLcdResetRecoverMs = 120;    // P4 HIGH
constexpr uint32_t kTouchResetPreMs = 100;      // P0 HIGH
constexpr uint32_t kTouchResetPulseMs = 120;    // P0 LOW
constexpr uint32_t kTouchResetRecoverMs = 120;  // P0 HIGH
constexpr uint32_t kTouchIntSettleMs = 120;     // after P2 HIGH
constexpr uint32_t kBacklightOnDelayMs = 200;   // before backlight on and P3 LOW

}  // namespace sarathi::board::pins
