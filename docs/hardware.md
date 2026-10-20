# Hardware: Elecrow CrowPanel 2.1" Rotary Display (DHE03921D)

ESP32-S3 (16 MB flash, 8 MB OPI PSRAM), ST7701S 480×480 round IPS panel on an RGB parallel
bus, CST8xx capacitive touch, rotary encoder with push button, PCF8574 I/O expander.

The values below are defined in code **only** in
[`firmware/lib/sarathi_board/board_pins.h`](../firmware/lib/sarathi_board/board_pins.h),
where each one cites its source. This page explains them, records what has been verified on
the device, and lists where the sources disagree. Build and flash instructions are in
[`building.md`](building.md).

## Status

What has been checked on a real board (October 2026):

| Subsystem | Status | Evidence |
|---|---|---|
| Chip, flash, PSRAM | ✅ verified | Boot log: ESP32-S3 rev v0.2, 2 cores @ 240 MHz, 16 MB flash (configured and physical), 8192 KB PSRAM |
| USB CDC serial log | ✅ verified | `ESP_LOGx` output on the USB IN port with `monitor_filters = direct` |
| Display: power-up, timing | ✅ verified | Full-screen fills and a centred ring: round, centred, no flicker, scrolling or line shift |
| Display: colour order | ✅ verified | Labelled red, green, blue and white fills correct with the R/B swap and MADCTL `0x08` |
| LVGL rendering | ✅ verified | Input test screen redraws without flicker or tearing |
| Knob rotation | ✅ verified | Clockwise counts up, one step per detent |
| Knob button | ✅ verified | Debounced press/release; click and double click |
| Touch | ✅ verified | Dot under the finger at the centre and near the edge (Y offset 20) |
| Backlight PWM | ✅ verified | Follows the knob from 5 % to 100 %; still readable at 5 % |
| Wi-Fi, OTA, rollback | ⏳ not started | Phase 2 |
| BLE | ⏳ not started | Phases 3–5 |
| Onboard LED (GPIO 43?) | ❓ unresolved | See open question 2 |

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

## Panel optics

From the panel datasheet, `vendor/crowpanel-2.1/Datasheet/2.1Inches Circular_P021B002-IPS(3.3V).pdf`:

| Item | Value |
|---|---|
| Size | 2.1" round IPS, 480 × 480 |
| Display area | 53.28 × 53.28 mm |
| Pixel pitch | 111 µm (9.0 px/mm) |
| Luminance | 300 cd/m² typical |
| Contrast ratio | 1000:1 typical (800 min) |
| Viewing angle | 85° all directions (CR ≥ 10) |

The theme legibility rules (ADR-0003) derive minimum font sizes from this pitch. 300 nit
is modest for direct sunlight, which is why those rules ask for high contrast.

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

## Memory and partitions

`firmware/partitions.csv`, 16 MB flash:

| Name | Type | Offset | Size | Use |
|---|---|---|---|---|
| `nvs` | data/nvs | 0x9000 | 20 KB | Settings, BLE bonds |
| `otadata` | data/ota | 0xE000 | 8 KB | Which OTA slot boots |
| `app0` | app/ota_0 | 0x10000 | 6 MB | Firmware slot A |
| `app1` | app/ota_1 | 0x610000 | 6 MB | Firmware slot B (OTA target, rollback) |
| `littlefs` | data/littlefs | 0xC10000 | ~3.9 MB | Fonts and small assets |
| `coredump` | data/coredump | 0xFF0000 | 64 KB | Crash dumps |

**Settings** are one 8-byte versioned blob (`sarathi_core/settings_codec.h`) under NVS
namespace `sarathi`, key `cfg`, written through `Preferences`. Saves are debounced: the
blob is written 2 s after the last change, so turning the brightness knob costs one flash
write, not one per detent (flash writes can disturb the RGB panel). An unknown version or
an out-of-range field falls back to the default. The boot log repeats
`settings loaded|defaults: …` for the first 10 s; each write logs `settings saved`.

Measured after the input bring-up (step D):

- **Firmware image:** 655,673 B, 10.4 % of a 6 MB slot.
- **Internal RAM (static):** 25,916 B, 7.9 %; 236 KB heap free at runtime.
- **PSRAM:** 1,367 KB in use of 8,192 KB. That is the panel framebuffer plus two LVGL draw
  buffers, 3 × 460,800 B, all allocated once at init.

Elecrow's image-heavy demo is about 6 MB, so keep image assets small and prefer vector
shapes and fonts.

## Performance (measured)

Measured on the device on 2026-10-20 with the Phase 1 Nav screen (full-circle route map,
auto-zoom, Classic theme) driven by the demo ride, from the `perf` serial log
(`docs/building.md`). Commit: Phase 1 step E.

| | Normal build | Stress build (`-DSARATHI_NAV_STRESS`) |
|---|---|---|
| What redraws | map once a second (new route window), labels as they change | whole map every refresh, heading +2° per frame |
| Frames rendered | 2-3.6 per second on Nav; 0 on a static screen (ringing call) | 9.4-10.8 per second (the ceiling) |
| Frame time, average | 35-45 ms | 87-103 ms |
| Frame time, worst | 87-102 ms (a full-screen map redraw) | 104 ms |
| Flush (copy to panel framebuffer), worst | 43 ms | 43 ms per frame |

- A full-screen redraw costs about 100 ms: roughly 45-60 ms of rendering and 42 ms of flush.
  The flush is the R/B swap plus `draw16bitRGBBitmap()` copying 460 KB into the panel
  framebuffer, both in PSRAM.
- So the map can redraw at most about 10 times a second, with the UI task fully busy. The
  normal build uses about 12 % of it on Nav.

Memory, steady over the runs (no growth, no fragmentation):

| | Value |
|---|---|
| Internal heap free | 216 KB (lowest seen 208 KB; largest free block 203-207 KB) |
| PSRAM free | 6,825 KB of 8,192 KB, unchanged (panel framebuffer and two LVGL buffers use about 1.35 MB) |
| Static internal RAM | 30.5 KB (9.3 %) |
| Firmware image | 915 KB (14.6 % of a 6 MB slot) |
| UI (Arduino loop) task stack | at least 3.6 KB never used |
| Knob task stack | at least 1.36 KB of 2 KB never used |

## Boot log

With `pio device monitor`, a healthy boot prints, in order:

```
[display] ready (480x480)
[touch] ready
[knob] ready
[main] LVGL ready (2 x 460800 B buffers in PSRAM)
[main] hello from sarathi | ESP32-S3 v0.2, 2 cores @ 240 MHz | flash 16 MB (chip 16 MB) | PSRAM 8192 KB (6825 KB free) | heap 236 KB free | up 201 s
```

The hello line repeats every second. `flash … (chip …)` compares the configured size with
the size read from the flash chip; they should match. `PCF8574 not responding`,
`CST8xx not found` or `RGB panel init failed` instead point at the I2C bus, the touch
controller or the panel.

## Open questions

1. **P3 wording.** The wiki calls P3 "LCD power supply" and only ever sets it HIGH; the
   lessons do the same. The schematic shows P3 driving the gate of a P-MOSFET with a pull-up,
   so HIGH leaves `VDD_LCD` **off** and LOW turns it on. The demo matches the schematic: it
   pulls P3 LOW at the end of `setup()`. The panel also has a second supply pin on 3.3V
   directly, which is probably why the lessons work without it. We follow the demo, and the
   display is verified with it; the open part is only why the wiki disagrees.
2. **GPIO 43.** Elecrow's code and wiki call it the onboard/indicator LED; the schematic
   names it `U0TXD` (`ESP_TXD`). Not used until resolved.

## Resolved discrepancies

- **R/B swap.** The demo swaps R and B; the lessons don't. The demo is right (verified).
- **Reset delays.** The lessons use shorter delays (50/100 ms) than the demo (100/120 ms).
  We use the demo's (verified).
- **Encoder A.** The encoder (`J1`) connects to nets `GPIO42`/`GPIO4`. The PCF8574 P6 net
  `ENCODER_A` connects to nothing else; it is a stray label. P6 is unused.
- **Touch Y offset.** The demo subtracts 20 from Y; the lessons don't. The demo is right
  (verified).
- **Stray Elecrow examples.** `example/Simple example/RGB_CODE` drives a NeoPixel strip on
  GPIO 48 (the panel's B2 line here), and `Encoder_code` uses GPIO 45/42/41 (45 and 41 are
  panel lines here). Neither is for this board.

## Power and environment

5 V / 1 A input, operating range −20 to 65 °C. Flash through the **USB IN** port (native USB).
Never hot-plug the FPC cable while powered.
