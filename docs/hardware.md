# Hardware: Elecrow CrowPanel 2.1" Rotary Display (DHE03921D)

ESP32-S3 (16 MB flash, 8 MB OPI PSRAM), ST7701S 480×480 round IPS panel on an RGB parallel
bus, CST8xx capacitive touch, rotary encoder with push button, PCF8574 I/O expander.

The values below are defined in code **only** in
[`firmware/lib/sarathi_board/board_pins.h`](../firmware/lib/sarathi_board/board_pins.h),
where each one cites its source. This page explains them and records where the sources
disagree.

## Sources

| Tag | Source |
|---|---|
| demo | `vendor/crowpanel-2.1/example/Arduino/RotaryScreen_2_1/RotaryScreen_2_1.ino`: Elecrow's LVGL demo for our toolchain |
| lesson | `vendor/crowpanel-2.1/example/Arduino/Examples/Lesson0[2-5]*/Board21.h` (all four identical) |
| sch | `vendor/crowpanel-2.1/Eagle_SCH&PCB/ESP32_Display_2.1(K)_Main_V1.0.sch` (Eagle XML; nets read directly) |
| wiki | [LVGL rotary guide](https://www.elecrow.com/wiki/2.1_Arduino_LVGL_Rotary_Guide.html) |

Last cross-checked: October 2026.

## Pin map

| Function | GPIO / address | Schematic net | Agrees |
|---|---|---|---|
| I2C SDA / SCL | 38 / 39 | `GPIO38-SDA` / `GPIO39-SCL` | demo, lesson, sch, wiki |
| PCF8574 | I2C 0x21 | U3, A0 → 3.3V, A1/A2 → GND | demo, lesson, sch, wiki |
| Touch (CST8xx) | I2C 0x15 | `TP_SDA`/`TP_SCL` on the shared bus | demo, lesson, wiki |
| ST7701S SPI CS / SCK / SDA | 16 / 2 / 1 | `LCD_CS` (GPIO16 = XTAL_32K_N), `LCD_SCK`, `LCD_SDA` | demo, lesson, sch |
| DE / VSYNC / HSYNC / PCLK | 40 / 7 / 15 / 41 | `DE`, `VSYNC`, `HSYNC` (GPIO15 = XTAL_32K_P), `PCLK` | all four |
| R0–R4 | 46, 3, 8, 18, 17 | `R1`–`R5` | all four |
| G0–G5 | 14, 13, 12, 11, 10, 9 | `G0`–`G5` | all four |
| B0–B4 | 5, 45, 48, 47, 21 | `B1`–`B5` | all four |
| Backlight PWM | 6 | `BL_PWM` → NPN Q3, low side of LED string | all four |
| Encoder A / B | 42 / 4 | `GPIO42` / `GPIO4` | demo, lesson, wiki |

The schematic numbers the red and blue lines from 1; the GFX library numbers them from 0.
The GPIOs are the same.

### PCF8574 pins

| Pin | Net | Use |
|---|---|---|
| P0 | `TP_RST` | Touch reset (pulsed during power-up) |
| P1 | `RW` | Unused |
| P2 | `TP_INT` | Touch interrupt line; Elecrow drives it HIGH as an output |
| P3 | `LED-ON` | Gate of P-MOSFET Q1 (3.3V → `VDD_LCD`), 10 kΩ pull-up: **LOW = `VDD_LCD` on** |
| P4 | `LCD_RST` | Panel reset |
| P5 | `ENCODER_SW` | Knob button, active low, pull-up |
| P6 | `ENCODER_A` | Unused by Elecrow's code (see below) |

## Panel timing and colour

- **PCLK** 12 MHz. **HSYNC and VSYNC:** polarity 1, front porch 10, pulse width 4,
  back porch 20. PCLK active on the rising edge (`pclk_active_neg = 0`). DE and PCLK idle low.
- **Bounce buffer:** 480 × 20 px.
- **Init sequence:** `st7701_type5_init_operations` from GFX Library for Arduino 1.6.7.
- **MADCTL** (`0x36`) = `0x08` (BGR), written after `gfx->begin()`.
- **R/B swap:** the demo swaps the R and B fields of every RGB565 pixel before drawing
  (it notes that ESP-IDF 5's RGB path needs this). The lessons don't. We follow the demo
  (`kSwapRedBlue = true`), and this is verified on hardware.

Wrong timing shows up as a black screen, scrolling, line shift or flicker.

## Power-up sequence

Implemented in `Display::Begin()`, following the demo (`setup()`, L470–579):

1. `Wire.begin(38, 39)`.
2. PCF8574 pin modes, then `begin()`.
3. P3 HIGH, wait 100 ms.
4. LCD reset on P4: HIGH 100 ms → LOW 120 ms → HIGH 120 ms.
5. Touch reset on P0: HIGH 100 ms → LOW 120 ms → HIGH 120 ms. Then P2 HIGH, wait 120 ms.
6. `gfx->begin()`, then MADCTL `0x08`, then fill black.
7. Wait 200 ms, start the backlight (GPIO 6, 5 kHz, 8-bit, duty 204 ≈ 80 %).
8. P3 LOW.

Touch `begin()` and `lv_init()` follow in later steps.

## Verified on hardware

Display bring-up was verified on the device in October 2026, using the timings in
`board_pins.h` and the power-up sequence above. The bring-up pattern (red, green, blue and
white fills, each labelled, then a centred ring) showed:

- RGB order correct with `kSwapRedBlue = true` and MADCTL `0x08`
- Ring round and centred in the visible circle
- No flicker, scrolling or line shift

## Discrepancies and open questions

1. **P3 polarity.** The wiki calls P3 "LCD power supply" and only ever sets it HIGH; the
   lessons do the same. The schematic shows P3 driving the gate of a P-MOSFET with a pull-up,
   so HIGH leaves `VDD_LCD` **off** and LOW turns it on. The demo matches the schematic: it
   pulls P3 LOW at the end of `setup()`. The panel also has a second supply pin on 3.3V
   directly, which is probably why the lessons work without it. **We follow the demo.**
2. **R/B swap.** The demo swaps; the lessons don't. **Resolved:** the demo is right; the
   swap is verified on hardware (see above).
3. **Reset delays.** The lessons use shorter delays (50/100 ms) than the demo (100/120 ms).
   We use the demo's.
4. **Encoder A.** The code reads the encoder on GPIO 42/4, but the schematic also routes a
   net named `ENCODER_A` to PCF8574 P6. To resolve when the encoder driver is written.
5. **GPIO 43.** Elecrow's code and wiki call it the onboard/indicator LED; the schematic
   names it `U0TXD` (`ESP_TXD`). Not used until resolved.
6. **GPIO 48.** `example/Simple example/RGB_CODE` drives a 5-LED NeoPixel strip on GPIO 48,
   which is the B2 line of the panel on this board. That example is not for this board.

## Power and environment

5 V / 1 A input, operating range −20 to 65 °C. Flash through the **USB IN** port (native USB).
Never hot-plug the FPC cable while powered.
