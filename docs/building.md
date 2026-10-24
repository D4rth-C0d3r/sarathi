# Building sārathi

This is the single source of truth for setting up a machine, building, flashing and testing.
Hardware details live in [`hardware.md`](hardware.md).

Supported hosts: **macOS, Linux and WSL**. (WSL can build but can't see USB devices by
default; flash from the host OS or use `usbipd-win`.)

## Prerequisites

| Tool | Needed for | Notes |
|---|---|---|
| git | everything | |
| Python 3.10–3.13 with `venv` | PlatformIO, repo tools | pioarduino doesn't support other versions. Debian/Ubuntu: `sudo apt install python3-venv` |
| Host C++ compiler (g++ or clang++) | `pio test -e native` | macOS: `xcode-select --install`; Debian/Ubuntu: `sudo apt install build-essential` |
| VS Code + PlatformIO extension | optional | The CLI is enough |
| JDK 17+ | Android app | Gradle uses the `java` on `PATH` (or `JAVA_HOME`) |
| Android SDK | Android app | Command-line tools or Android Studio; see [Android](#android) |

## First-time setup

```bash
git clone --recurse-submodules --shallow-submodules https://github.com/D4rth-C0d3r/sarathi.git
cd sarathi
tools/setup.sh .
```

`tools/setup.sh` is safe to re-run; every step checks before acting. From a clone it:

1. Checks the prerequisites above.
2. Installs **PlatformIO Core** in its own venv at `~/.platformio/penv` (via PlatformIO's
   official installer), unless `pio` is already installed. System Python is untouched.
3. Pre-downloads the pinned pioarduino platform into `~/.platformio` (large).
4. Creates the project venv **`.venv`** and installs `pre-commit` into it.
5. Fetches `vendor/crowpanel-2.1` if the submodule is missing.
6. Asks before each machine-level change: VS Code extensions, symlinking `pio` into
   `~/.local/bin`, and on Linux installing PlatformIO's udev rules and adding you to
   `dialout` (both need `sudo`; log out and in afterwards).
7. Asks you to plug in the board and looks for the ESP32-S3's native USB port.

It never touches system Python, `pip --user`, shell rc files or global git config. The
script was first written to bootstrap the repo, so its final "Next:" hints (create a GitHub
repo, paste a prompt) don't apply to a clone; ignore them.

If `pio` isn't found in a new terminal, add this line to your shell rc yourself:

```bash
export PATH="$HOME/.platformio/penv/bin:$PATH"
```

Then enable the git hooks:

```bash
source .venv/bin/activate
pre-commit install
```

Two venvs, never mixed: PlatformIO in `~/.platformio/penv`, repo Python tools in `.venv`.
Add repo tool dependencies to `tools/requirements.txt`; never `pip install` globally.

If you cloned without submodules: `git submodule update --init --depth 1`.

## Firmware

All commands run from `firmware/`.

```bash
pio run                                       # build the device image (default env: crowpanel21)
pio run -t upload                             # build and flash over the USB IN port
pio device monitor                            # serial log (native USB CDC; Ctrl+C to quit)
pio device monitor -f esp32_exception_decoder # only when decoding a crash backtrace
pio test -e native                            # host unit tests for sarathi_core / sarathi_proto
pio test -e crowpanel21                       # on-device tests (test/test_device_*); replaces the app
pio run -t compiledb                          # write compile_commands.json for clangd / clang-tidy
pio run -t clean                              # remove build outputs
```

- **Environments.** `crowpanel21` is the device and the default, so a plain `pio run`
  never tries to build the Arduino `main.cpp` on the host. `native` only runs tests; it
  doesn't build `src/` and ignores the hardware libraries.
- **First build** downloads the pinned platform and toolchain (about 10 minutes on a
  typical connection). Later builds take seconds.
- **Flashing** goes through the **USB IN** port, which is the ESP32-S3's native USB, so no
  driver is needed. Use a data cable, not a charge-only one.
- **Expected serial output** after boot (one hello line per second):

  ```
  [...][I][display.cpp:..] Begin(): [display] ready (480x480)
  [...][I][touch.cpp:..] Begin(): [touch] ready
  [...][I][knob.cpp:..] Begin(): [knob] ready
  [...][I][main.cpp:..] setup(): [main] LVGL ready (2 x 460800 B buffers in PSRAM)
  [...][I][main.cpp:..] LogHello(): [main] hello from sarathi | ESP32-S3 v0.2, 2 cores @ 240 MHz | flash 16 MB (chip 16 MB) | PSRAM 8192 KB (6825 KB free) | ...
  ```

  The first field is milliseconds since boot.

### On-device tests

`test/test_device_*` runs on the board itself (Unity over the serial port). Today that is
`test_device_ota`:

- every golden OTA vector with real ES256 and SHA-256 (mbedTLS) on the ESP32-S3
- the production OTA key parses; plain `http://` is refused
- **with Wi-Fi:** joins your network, then fetches a pinned vector from GitHub over HTTPS,
  through the github.com → raw.githubusercontent.com redirect. That checks the root CA
  bundle on both hosts and verifies the manifest. It also reports heap use.

```bash
pio test -e crowpanel21 -v                 # no network test (it is reported as skipped)

# With the network test. The credentials come from your shell at build time; they are
# never committed, and the firmware never logs them.
export SARATHI_WIFI_SSID='your network' SARATHI_WIFI_PASSWORD='its password'
pio test -e crowpanel21-test -v
unset SARATHI_WIFI_SSID SARATHI_WIFI_PASSWORD
pio run -e crowpanel21-test -t clean       # the test build holds the credentials

pio run -e crowpanel21 -t upload           # put the app back afterwards
```

`-v` shows the measurements the test prints. The test firmware replaces the app, so flash
the app again when you're done.

Spaces in the network name or password are fine. A `'`, `"` or `\` won't survive the build
flags, so use a test network if yours has one.

### Desktop simulator

Runs the same `sarathi_ui` screens and `sarathi_core` logic on your desktop in a 480×480
window, with everything outside the round glass masked black. Useful for layout, themes
and input logic without flashing the board.

It needs the SDL2 development package once (Linux):

```bash
sudo apt install libsdl2-dev   # Debian/Ubuntu; ~45 small -dev packages
```

Then, from `firmware/`:

```bash
pio run -e sim            # build
pio run -e sim -t exec    # build and run (or run .pio/build/sim/program)
```

| Desktop | Device equivalent |
|---|---|
| ← / ↓ arrow, mouse wheel down | knob, one detent anticlockwise (−1) |
| → / ↑ arrow, mouse wheel up | knob, one detent clockwise (+1) |
| Enter | knob button: click activates, double-click = next screen, hold 0.8 s = leave the input test |
| Left mouse button inside the circle | touch |
| S | pause/resume the demo ride (the bike stops, so Settings unlocks) |

Both the device and the simulator replay a scripted demo ride (about 4.5 minutes, looping):
navigation with a route window, a traffic-light stop, an incoming call at 60 s, and a
playlist. `pio run -e sim -t exec` runs it in real time; to replay faster, run the program
directly: `.pio/build/sim/program --speed 10`. Close the window to quit. The simulator has no backlight, panel timing or ESP32-S3
performance limits: check speed, flicker and colours on the device. Colours are authored as
true RGB; only the device flush swaps red and blue.

Settings persist like on the device: changes are written to `sim_settings.bin` in the
working directory (git-ignored) 2 s after the last change and loaded at start. Delete the
file to go back to defaults. The backlight (brightness, night dimming, screen timeout) is
shown as a black overlay.

**Screenshot tour.** `.pio/build/sim/program --tour DIR` walks every screen with scripted
knob input (Nav, an incoming call, Music, Settings, the theme list, switching to the HUD
theme, adjusting brightness, day/night, imperial units, the scrolling settings list) and
saves a BMP of each stop in `DIR`, then exits. It always starts from default settings and
keeps its own settings file in `DIR`. It runs headless too:
`SDL_VIDEODRIVER=dummy .pio/build/sim/program --tour /tmp/tour`.

**Theme gallery.** `.pio/build/sim/program --gallery DIR` saves every screen in every
registered theme as `DIR/<theme>_<n>_<screen>.bmp`, then exits:

- Nav, call ringing, call active, Music, Settings, the theme list, the input test
- Wi-Fi setup (waiting, then joined)
- Software update (offered, downloading, failed)

Use it to review a new or changed theme against the ADR-0003 checklist. It also starts from
defaults and runs headless.

**Wi-Fi setup and updates in the simulator** use a stand-in for the radio, the update server
and the phone:

- **Wi-Fi setup:** the access point "starts" at once, and a pretend phone sends "Home Wi-Fi"
  about 6 s later.
- **Check for update:** finds version 0.3.0 (1.5 MB) and "downloads" it in about 2 s.
  "Restarting" just resets the update screen.
- **Before a network is saved:** Check for update says to set up Wi-Fi first.

### Performance overlay (device)

To see LVGL's frame rate, CPU load and render time on the board (for example while the Nav
map redraws), build with the overlay:

```bash
PLATFORMIO_BUILD_FLAGS=-DSARATHI_PERF_OVERLAY pio run -e crowpanel21 -t upload
```

The numbers appear at the bottom of the screen. A normal `pio run -t upload` removes it.

Every build also logs a `perf` line every 5 s on the serial monitor:

```
[main] perf normal | frames 15/5s | frame avg … ms max … ms | flush avg … ms max … ms (n) |
       heap int … KB free (min …, largest …) | psram … KB free (min …) | stack free ui … B, knob … B
```

- **frame:** one LVGL refresh that actually drew something (layout, render and flush to the
  panel framebuffer), from `LV_EVENT_REFR_START` to `LV_EVENT_RENDER_READY`.
- **flush:** the copy into the panel framebuffer alone (`n` = flushes in the period).
- **min:** the lowest free memory seen since boot. **largest:** the biggest free block.
- **stack free:** the least stack each task has had spare (its high-water mark).

To measure the worst case for a smoothly rotating map, build the stress variant, which
re-projects and redraws the route on every refresh:

```bash
PLATFORMIO_BUILD_FLAGS=-DSARATHI_NAV_STRESS pio run -e crowpanel21 -t upload
```

### Pinned versions

The ESP32 Arduino core, LVGL and the board libraries are pinned to Elecrow's tested versions
in `firmware/platformio.ini`; the table and reasons are in `CLAUDE.md` ("Board
configuration"). Upgrade only deliberately, in a dedicated PR, after testing on the device.

## Lint and format

```bash
source .venv/bin/activate
pre-commit run --all-files    # whitespace, YAML/JSON, private keys, clang-format, shellcheck, codespell
```

`pre-commit run --all-files` only checks files git tracks; for new files, use
`pre-commit run --files <paths>` or stage them first. `.clang-tidy` is configured but needs
`compile_commands.json` (see above).

## CI

GitHub Actions runs on every pull request and every push to `main`:

| Workflow | Jobs | Same as running locally |
|---|---|---|
| `firmware` | **native tests**; **build crowpanel21** (caches `~/.platformio`); **build simulator**; **OTA manifest vectors** | `pio test -e native`; `pio run -e crowpanel21`; `pio run -e sim`; `python tools/ota_manifest.py check-vectors docs/ota/vectors` |
| `pre-commit` | all hooks on all files | `pre-commit run --all-files` |
| `android` | lint, unit tests, debug APK, ktlint, detekt | see [Android](#android) |

The **build crowpanel21** job also fails if `firmware.bin` exceeds 90 % of its 6 MB OTA
slot (ADR-0004), and writes the size to the run summary. Check locally after a build, from
`firmware/`: `python ../tools/check_image_size.py .pio/build/crowpanel21/firmware.bin
partitions.csv`.

Each `firmware` run uploads an artifact `firmware-<short sha>` with `firmware.bin` (to flash)
and `firmware.elf` (to decode crash backtraces), kept for 30 days. Find it on the run's
summary page under **Artifacts**.

CI uses PlatformIO 6.2.0 and the pre-commit version in `tools/requirements.txt`, matching
the local setup. Actions are pinned to commit SHAs. The `actionlint` hook checks workflow
files before you push.

## Android

The companion app lives in `android/`: Kotlin, Jetpack Compose, Gradle (via the wrapper;
no Gradle install needed). Modules:

| Module | Kind | Purpose |
|---|---|---|
| `:app` | Android application | UI (Compose, Material 3); one placeholder screen for now |
| `:core:protocol` | Kotlin/JVM library | Message encode/decode against `docs/protocol/vectors/` (Phase 4) |
| `:core:ble` | Android library | BLE scan, bond and GATT transport (Phase 4) |

Versions are in `android/gradle/libs.versions.toml`.

### Android SDK

You need JDK 17+ and an Android SDK. Either install Android Studio (it bundles the SDK), or
install only the command-line tools, user-level, without sudo:

```bash
# Get the current "commandlinetools-linux-*_latest.zip" link from
# https://developer.android.com/studio#command-line-tools-only, then:
mkdir -p ~/Android/Sdk/cmdline-tools
unzip commandlinetools-linux-*_latest.zip -d /tmp/cmdline-tools
mv /tmp/cmdline-tools/cmdline-tools ~/Android/Sdk/cmdline-tools/latest
~/Android/Sdk/cmdline-tools/latest/bin/sdkmanager \
  "platform-tools" "platforms;android-37.0" "build-tools;37.0.0"
```

Point Gradle at the SDK with either `export ANDROID_HOME=~/Android/Sdk`, or a
`android/local.properties` file (gitignored) containing `sdk.dir=/home/<you>/Android/Sdk`.

### Commands

All from `android/`:

```bash
./gradlew lint testDebugUnitTest assembleDebug   # Android lint, unit tests, debug APK
./gradlew ktlintCheck detekt                     # style and static analysis
./gradlew ktlintFormat                           # auto-fix ktlint findings
./gradlew installDebug                           # install on a USB-connected phone (adb)
```

`testDebugUnitTest` also runs `:core:protocol`'s JVM tests. The debug APK is written to
`app/build/outputs/apk/debug/app-debug.apk`.

## Troubleshooting

**Serial log lines are cut off at the start.**
The `esp32_exception_decoder` monitor filter drops the beginning of log lines. The project
default is `monitor_filters = direct`. Use `pio device monitor -f esp32_exception_decoder`
only when you need a crash backtrace decoded.

**Upload can't connect, or no serial port appears.**
- Check the cable carries data, and that you're on the **USB IN** port.
- Force download mode: hold **BOOT**, plug in USB, release BOOT, then upload again.
- Linux: `permission denied` on `/dev/ttyACM*` means the udev rules or `dialout` membership
  are missing; re-run `tools/setup.sh .` and accept those steps, then log out and in.
- WSL can't see USB by default; flash from the host OS.

**No serial output at all after flashing.**
The board logs over native USB CDC (`ARDUINO_USB_CDC_ON_BOOT=1`). The port re-enumerates
after reset; reopen `pio device monitor` if it was open during the upload.

**Build fails with `cannot find .../*.c.o` or `firmware.elf does not exist`.**
Two builds wrote to `firmware/.pio/` at the same time, for example the CLI and the VS Code
PlatformIO sidebar, or a build running while a formatter rewrote a source file. Re-run
`pio run` (or `pio run -t clean && pio run`), and don't build from two places at once.

**`pio run -v` fails with `TypeError: unsupported operand type(s) for +: '_Null' and 'str'`.**
A bug in the platform's verbose command printing while building `firmware.bin`; the compile
itself is fine. Use plain `pio run`. To see the exact compiler flags, run
`pio run -t compiledb` and read `compile_commands.json`.

**The editor shows errors like `Unknown argument '-mlongcalls'` or missing `stddef.h`.**
clangd is reading the Xtensa GCC flags from `compile_commands.json`. Harmless: `pio run` and
`pio test -e native` are the source of truth.

**`pio: command not found`.**
Add `~/.platformio/penv/bin` to your `PATH` (see First-time setup), or let `setup.sh`
symlink `pio` into `~/.local/bin`.

**Black screen, scrolling, wrong colours or touch offset.**
See [`hardware.md`](hardware.md): panel timing, the R/B swap and the touch Y offset are all
documented and verified there. Never hot-plug the display FPC cable while powered.
