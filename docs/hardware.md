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
| Encoder A / B | 42 / 4 | `GPIO42` / `GPIO4` from encoder `J1`, 10 kΩ pull-ups | demo, lesson, sch, wiki |

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
| P6 | `ENCODER_A` | Stray label, connected to nothing else; unused |

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

After `Display::Begin()`, `main.cpp` starts touch (`Touch::Begin()`), the knob task and
LVGL (`lv_init()`), in that order.

## Knob

- **Rotation:** encoder `J1` (EC28A), A → GPIO 42, B → GPIO 4, external 10 kΩ pull-ups.
  A task pinned to core 0 samples both pins every 2 ms (demo) and decodes them with
  Elecrow's quadrature table, 4 transitions per detent (Lesson02). It posts ±1 per detent
  to a 16-entry queue; the UI task drains it. Contact bounce cancels out in the table.
- **Button:** PCF8574 P5 (`ENCODER_SW`), active low, 10 kΩ pull-up. Read over I2C from the
  UI task only (the touch controller shares the bus). 50 ms debounce; a second completed
  press within 300 ms is a double click (demo).
- Logic lives in `sarathi_core` (`KnobDecoder`, `ButtonDebouncer`) with native tests.

## Touch

- CST8xx at 0x15 on the shared I2C bus (`Adafruit CST8XX` library), polled by LVGL from the
  UI task. The interrupt line (P2) isn't used.
- Correction (`sarathi_core::TouchFilter`, from the demo): subtract 20 from Y, clamp to
  0–479, and ignore moves under 4 px while the finger is down.

## Backlight

GPIO 6, LEDC 5 kHz, 8-bit. `Backlight::SetPercent()` clamps to 5–100 % (at 0 % the screen
is invisible) and maps with `percent × 255 / 100`, as the demo does. Starts at 80 % (duty 204).

## LVGL

Two full-screen RGB565 draw buffers (2 × 460,800 B) in PSRAM, allocated once in
`lvgl_port::Init()`, partial render mode. The flush callback swaps R/B in place and copies
the area into the panel framebuffer with `draw16bitRGBBitmap()`. Labels use
`lv_label_set_text_static()` with fixed buffers, because `lv_conf.h` routes LVGL's
allocations to the C heap.

## Verified on hardware

Display bring-up was verified on the device in October 2026, using the timings in
`board_pins.h` and the power-up sequence above. The bring-up pattern (red, green, blue and
white fills, each labelled, then a centred ring) showed:

- RGB order correct with `kSwapRedBlue = true` and MADCTL `0x08`
- Ring round and centred in the visible circle
- No flicker, scrolling or line shift

Input bring-up (step D) was verified on the device in October 2026 with the input test
screen:

- Knob: clockwise increases the count, one step per detent; backlight follows, still
  visible at 5 %
- Button: debounced press/release, click and double click detected
- Touch: the dot sits under the finger at the centre and near the edge (Y offset 20 is right)
- No flicker or tearing while labels and the touch dot update

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
4. **Encoder A.** **Resolved:** the encoder (`J1`) connects to nets `GPIO42`/`GPIO4`. The
   PCF8574 P6 net `ENCODER_A` connects to nothing else; it is a stray label. P6 is unused.
7. **Touch Y offset.** The demo subtracts 20 from Y; the lessons don't. **Resolved:** the
   demo is right; verified on hardware (see above).
8. **Simple example encoder.** `example/Simple example/Encoder_code` uses GPIO 45/42/41,
   but 45 (B1) and 41 (PCLK) are panel lines on this board. Like `RGB_CODE`, that example
   is not for this board.
5. **GPIO 43.** Elecrow's code and wiki call it the onboard/indicator LED; the schematic
   names it `U0TXD` (`ESP_TXD`). Not used until resolved.
6. **GPIO 48.** `example/Simple example/RGB_CODE` drives a 5-LED NeoPixel strip on GPIO 48,
   which is the B2 line of the panel on this board. That example is not for this board.

## Power and environment

5 V / 1 A input, operating range −20 to 65 °C. Flash through the **USB IN** port (native USB).
Never hot-plug the FPC cable while powered.
