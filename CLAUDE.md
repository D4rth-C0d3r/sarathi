# CLAUDE.md — sārathi

Guidance for Claude Code working in this repo. Area-specific rules live in
`firmware/CLAUDE.md` and `android/CLAUDE.md`. Read those too when working in those trees.

## What this project is

**sārathi** (repo: `sarathi`) is an open-source motorcycle navigation and dash companion.
In Sanskrit, a sārathi is a charioteer, as Krishna was Arjuna's.

- **Device:** Elecrow CrowPanel 2.1" Rotary Display (DHE03921D).
- **Phone:** Android companion app (Kotlin). iOS is out of scope.
- **Features:** turn-by-turn navigation, caller ID with accept/reject, music control with
  track info, settings, and OTA updates over Wi-Fi and BLE.

### Core architecture (ADR-0001: `docs/adr/0001-ble-only-phone-as-brain.md`)
The ESP32-S3 has **BLE only**: no Classic Bluetooth, so no HFP or A2DP.
**The phone is the brain; the device is a BLE display and remote.**
Call and music audio stay on the helmet intercom. Never propose designs that need
Classic Bluetooth audio on the device.

## Current status

- **Phase 0 (bring-up).** The repo skeleton exists; most trees are still empty.
- Some commands below won't work until their tree is scaffolded. Say so instead of
  inventing workarounds.

Roadmap:
0. Bring-up
1. UI shell + settings + simulator
2. Wi-Fi OTA + rollback
3. ChronosESP32 quick win
4. Own Android app + protocol
5. BLE OTA
6. Bike integration
7. Extras: map snapshots, GPS, trip stats

## Repo map

```
docs/            protocol/ (protocol.md, schema/, vectors/), adr/, hardware.md, building.md, safety.md
firmware/        PlatformIO project
  src/main.cpp   wiring only, no logic
  lib/sarathi_core      app state, logic        (pure C++17, no hardware)
  lib/sarathi_proto     message encode/decode   (pure C++17, no hardware)
  lib/sarathi_board     pins, display, touch, knob, backlight, PCF8574 (board_pins.h lives here)
  lib/sarathi_link      BLE (NimBLE) transport
  lib/sarathi_ota       Wi-Fi + BLE OTA, rollback
  lib/sarathi_settings  NVS/Preferences
  lib/sarathi_ui        LVGL screens
  test/                 native unit tests
android/         app, core/protocol, core/ble, feature/{navigation,media,calls,ota}
hardware/        enclosure/, wiring/
tools/           ble_client.py (bleak), release.py, setup scripts
vendor/crowpanel-2.1/   Elecrow examples (git submodule, shallow), READ-ONLY
```

## Commands

### Firmware (run from `firmware/`)
```bash
pio run -e crowpanel21              # build for the device
pio run -e crowpanel21 -t upload    # flash over the USB IN port
pio device monitor                  # serial log (115200, native USB CDC)
pio test -e native                  # host unit tests (core + proto)
pio run -e sim                      # LVGL SDL simulator (once added)
```

### Android (run from `android/`)
```bash
./gradlew lint testDebugUnitTest    # lint + unit tests
./gradlew ktlintCheck detekt        # style + static analysis
./gradlew installDebug              # install on connected phone
```

### Repo-wide
```bash
git submodule update --init --depth 1   # fetch vendor/ after a fresh clone
source .venv/bin/activate               # Windows: .venv\Scripts\Activate.ps1
pre-commit run --all-files              # formatting and lint hooks
python tools/ble_client.py --help       # talk to the device from a PC (bleak)
```

Python: repo tools run from the project `.venv` only; add their deps to
`tools/requirements.txt`. Never `pip install` globally, with `--user` or with
`--break-system-packages`. PlatformIO lives in its own venv at `~/.platformio/penv`.
This is the user's work laptop, so ask before any machine-level change (sudo, PATH,
global git config, system packages).

First-time machine setup: `tools/setup.sh` (macOS/Linux/WSL) or `tools/setup.ps1` (Windows).
Full details live in `docs/building.md`. Keep that file the single source of truth.

## Board configuration (verified against the Elecrow wiki, Oct 2026)

Elecrow's tested toolchain (versions confirmed in `vendor/crowpanel-2.1/example/Arduino/libraries/*/library.properties`;
Elecrow's `lv_conf.h` is there too). Pin these exact versions in `platformio.ini`. Upgrade only
deliberately, in a dedicated PR, and only after testing on the device.

| Component | Version | PlatformIO |
|---|---|---|
| ESP32 Arduino core | 3.3.8 (ESP-IDF 5.5.4) | `platform = https://github.com/pioarduino/platform-espressif32/releases/download/55.03.38/platform-espressif32.zip` |
| LVGL | 9.1.0 | `lvgl/lvgl@9.1.0` (`LV_COLOR_DEPTH 16`) |
| GFX Library for Arduino | 1.6.7 | `moononournation/GFX Library for Arduino@1.6.7` |
| Adafruit CST8XX (touch) | 1.1.1 | `adafruit/Adafruit CST8XX Library@1.1.1` |
| Adafruit BusIO | 1.16.2 | `adafruit/Adafruit BusIO@1.16.2` |
| PCF8574 library (Renzo Mischianti) | 2.3.7 | `xreef/PCF8574 library@2.3.7` |

Arduino IDE settings → `platformio.ini` equivalents:
- Board `ESP32S3 Dev Module` → `board = esp32-s3-devkitc-1`, with overrides below.
- Flash 16MB, QIO 80MHz → `board_upload.flash_size = 16MB`, `board_build.flash_mode = qio`,
  `board_build.f_flash = 80000000L`.
- OPI PSRAM → `board_build.arduino.memory_type = qio_opi`, `-DBOARD_HAS_PSRAM`.
- USB CDC On Boot enabled, Hardware CDC+JTAG → `-DARDUINO_USB_MODE=1 -DARDUINO_USB_CDC_ON_BOOT=1`.
- 240MHz CPU; upload speed 921600.
- Partitions: our own `firmware/partitions.csv`, not Elecrow's `elecrow_s3.csv`.

Flashing goes through the **USB IN** port, which is native USB, so no driver is needed.
If auto-download fails, hold **BOOT**, plug in USB, release, and upload again.
Never hot-plug the FPC cable while the device is powered.

## Hardware facts and gotchas

Pins are in `board_pins.h` only. Full details go in `docs/hardware.md`.

- **Display:** ST7701S, 480×480, RGB565 over the RGB parallel bus.
  - Init commands go over 3-wire SPI (CS 16, SCK 2, SDA 1).
  - Elecrow timing: PCLK **12 MHz**; hsync and vsync front/pulse/back porch **10/4/20**;
    DMA bounce buffer **480×20** px.
  - Wrong timing causes a black screen, scrolling, line shift or flicker.
- **Colour order:** Elecrow's flush callback swaps the R and B fields of RGB565 and uses
  `MADCTL 0x36 = 0x08`. If colours look wrong, check this before touching UI assets.
- **Touch:** CST8xx at I2C **0x15**, sharing the bus with the PCF8574 (SDA 38 / SCL 39).
  Elecrow subtracts **20 from Y** to match the visible area, and ignores moves under 4 px.
- **PCF8574 @ 0x21:**

  | Pin | Function |
  |---|---|
  | P0 | Touch reset |
  | P2 | Touch INT |
  | P3 | `LED-ON`: gate of P-MOSFET Q1 switching `VDD_LCD`, **active low** (schematic) |
  | P4 | LCD reset |
  | P5 | Encoder button (active low, pull-up) |
  | P6 | Net `ENCODER_A` in the schematic; unused by Elecrow's code (see `docs/hardware.md`) |

- **Encoder:** A on GPIO 42, B on GPIO 4.
  - Elecrow polls it every 2 ms in a task on core 0 and pushes events to a queue.
  - Button handling: 50 ms debounce; a second press within 300 ms is a double-click.
- **Backlight:** GPIO 6 PWM. At 0% the screen is invisible. Clamp the user minimum to
  about 5%.
- **GPIO 43** drives the ambient/indicator LED in Elecrow's demo, but the schematic names it
  `U0TXD` (net `ESP_TXD`); Elecrow's UART lesson uses 43/44 as TX/RX. Don't use either until
  this is resolved.
- **Power-up order** (Elecrow's demo, implemented in `sarathi_board/display.cpp`):
  `Wire.begin(38,39)` → PCF8574 begin → P3 HIGH → LCD reset (P4) → touch reset (P0, P2 HIGH)
  → `gfx->begin()` → MADCTL → backlight on → **P3 LOW** (`VDD_LCD` on) → touch begin
  → `lv_init()`. The wiki and lessons only set P3 HIGH; see `docs/hardware.md`.
- **LVGL buffers:** 480×480×2 B = 460,800 B each. Allocate two in PSRAM
  (`MALLOC_CAP_SPIRAM`) once at init, using `LV_DISPLAY_RENDER_MODE_PARTIAL`.
- **Image size:** Elecrow's image-heavy demo is about 6 MB, which barely fits our 6 MB OTA
  slots. Keep image assets small; prefer vector shapes and fonts over full-screen bitmaps.
- **Power:** 5V/1A in, operating range −20 to 65°C. Dim the backlight on high chip temperature.

## Engineering rules

### Architecture
- `sarathi_core` and `sarathi_proto` include **no** Arduino, ESP-IDF, LVGL or NimBLE
  headers. They must build and test under `pio test -e native`.
- Dependencies flow inward: `ui/link/ota/settings/board → core/proto`. Never the reverse.
- `main.cpp` only constructs and wires modules together.

### Concurrency and memory (firmware)
- LVGL is called **only** from the UI task. Other tasks (encoder, BLE) post to queues.
- BLE callbacks copy data and enqueue it. They never block, allocate, log heavily or touch LVGL.
- No heap allocation in steady state. Allocate at init and use fixed-size buffers or queues.
- No Arduino `String` in libraries. Use `std::string_view` or fixed char buffers.
- Log with `ESP_LOGx(TAG, ...)`, one `TAG` per module.
- Flash writes (OTA, NVS) can make the RGB panel flicker or shift. Show a static
  "Updating…" screen during OTA and avoid frequent NVS writes; debounce settings saves.

### Hardware
- Pin numbers, I2C addresses and panel timings exist **only** in
  `lib/sarathi_board/board_pins.h`. Each must cite its source (`vendor/` file or wiki section).
- Never guess a pin, address or timing. If `vendor/` and the wiki disagree, or neither says,
  stop and ask.
- Copy what you need from `vendor/` into `sarathi_board`, keeping attribution. Never edit
  `vendor/`. Don't vendor Elecrow's SquareLine UI or its large image assets.

### Protocol (phone ↔ device)
- One custom GATT service: a write characteristic (phone→device) and a notify
  characteristic (device→phone). MTU ≥ 247. JSON messages.
- Message types: `hello`, `nav`, `nav_end`, `call`, `call_action`, `media`, `media_cmd`,
  `time`, `phone_batt`, `ota_begin`, `ota_chunk`, `ota_end`.
- The protocol version is separate from the firmware version and is sent in `hello`.
- **Any protocol change: update `docs/protocol/protocol.md`, `schema/` and `vectors/`
  FIRST.** Firmware and Android tests both run against the same golden vectors.

### Rider safety and UX
- Everything must be operable with the knob alone (gloves). Touch is optional.
- One-second-glance UI: large type, high contrast, at most one primary item per screen.
- Content stays inside the 480 px circle. Keep a safe margin from the edge.
- Settings menus are locked while moving.
- Don't add features that encourage interacting with the device while riding.

### Security
- Control and OTA writes require a bonded BLE link. Bonding is confirmed by a knob press.
- Firmware images are signed. Signing keys exist only in GitHub Actions secrets.
  Never commit keys, and never generate or print real keys.
- OTA uses rollback. Mark the app valid only after the UI and BLE have started successfully.

### Style
- **C++:** C++17, `.clang-format` (Google base, 100 cols), `.clang-tidy`, and one
  namespace per module (`sarathi::core`, `sarathi::ui`, …).
- **Kotlin:** ktlint + detekt; Compose, MVVM, multi-module.
- **Commits:** Conventional Commits (`feat(firmware): …`, `fix(android): …`,
  `docs(protocol): …`). SemVer; changelog via release-please.

## How to work in this repo

1. **Plan before coding.** For anything beyond a trivial fix, state the plan and the
   files you'll touch, then wait for approval.
2. **Work in small steps.** Each step should build, pass tests and be committable on its own.
3. **Verify before finishing.** Run the relevant build and test commands. For firmware,
   at least `pio run -e crowpanel21` and `pio test -e native`. Report what you ran and
   the result. You can't see the screen, so ask the user to confirm on-device behaviour.
4. **Don't change scope silently.** If a task needs a new dependency, version bump,
   partition change, protocol change or architecture change, flag it and ask first.
5. **Keep docs in sync.** Update `docs/building.md`, `docs/hardware.md` or an ADR in the
   same change as the code. Record significant decisions as a new ADR.

## References

- Product page (Robocraze): https://robocraze.com/products/elecrow-crowpanel-rotary-screen-2-1inch-esp32-hmi-display-480-480-round-touch-knob
- Elecrow wiki: https://www.elecrow.com/wiki/CrowPanel_2.1inch-HMI_ESP32_Rotary_Display_480_IPS_Round_Touch_Knob_Screen.html
- Arduino course index: https://www.elecrow.com/wiki/2.1_inch_Rotary_Display_Arduino_Course_Index.html
- LVGL rotary guide (versions, settings, init order): https://www.elecrow.com/wiki/2.1_Arduino_LVGL_Rotary_Guide.html
- User manual: https://www.elecrow.com/download/product/DHE03921D/User_Manual(Rotary_Display).pdf
- ESP32-S3 datasheet: https://www.elecrow.com/download/product/DHE03921D/esp32-s3_datasheet_en.pdf
- Elecrow examples (also at `vendor/crowpanel-2.1`): https://github.com/Elecrow-RD/CrowPanel-2.1inch-HMI-ESP32-Rotary-Display-480-480-IPS-Round-Touch-Knob-Screen
- pioarduino platform: https://github.com/pioarduino/platform-espressif32
- LVGL 9.1 docs: https://docs.lvgl.io/9.1/
- NimBLE-Arduino: https://github.com/h2zero/NimBLE-Arduino
- ChronosESP32 (Phase 3): https://github.com/fbiego/chronos-esp32
