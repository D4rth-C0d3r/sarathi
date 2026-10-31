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
pio run -e crowpanel21-chronos -t upload      # the same with the Chronos phone link (below)
pio run -e crowpanel21-phone -t upload        # the same with our own phone link (protocol v1)
pio device monitor                            # serial log (native USB CDC; Ctrl+C to quit)
pio device monitor -f esp32_exception_decoder # only when decoding a crash backtrace
pio test -e native                            # host unit tests for sarathi_core / sarathi_proto
pio test -e crowpanel21                       # on-device tests (test/test_device_*); replaces the app
pio run -t compiledb                          # write compile_commands.json for clangd / clang-tidy
pio run -t clean                              # remove build outputs
```

- **Environments:**
  - `crowpanel21` is the device and the default, so a plain `pio run` never tries to build
    the Arduino `main.cpp` on the host. It runs the scripted demo ride.
  - `crowpanel21-chronos` is the same device with the Chronos app as its phone link
    (ADR-0005). Releases ship this one.
  - `crowpanel21-phone` is the same device with our own phone link, protocol v1 (ADR-0007).
    Until the Android app speaks it, `tools/ble_client.py` plays the phone (below).
  - `native` only runs tests; it doesn't build `src/` and ignores the hardware libraries.
- **First build** downloads the pinned platform and toolchain (about 10 minutes on a
  typical connection). Later builds take seconds.
- **Flashing** goes through the **USB IN** port, which is the ESP32-S3's native USB, so no
  driver is needed. Use a data cable, not a charge-only one.
- **Expected serial output** after boot (a hello line each second for the first 10 s, then a `perf` line every 5 s):

  ```
  [...][I][display.cpp:..] Begin(): [display] ready (480x480)
  [...][I][touch.cpp:..] Begin(): [touch] ready
  [...][I][knob.cpp:..] Begin(): [knob] ready
  [...][I][main.cpp:..] setup(): [main] LVGL ready (2 x 460800 B buffers in PSRAM)
  [...][I][main.cpp:..] LogHello(): [main] hello from sarathi | ESP32-S3 v0.2, 2 cores @ 240 MHz | flash 16 MB (chip 16 MB) | PSRAM 8192 KB (6825 KB free) | ...
  ```

  The first field is milliseconds since boot.

### Phone link: the Chronos app (Phase 3)

Until our own Android app exists (Phase 4), the `crowpanel21-chronos` build gets navigation,
calls, music, the time and the phone's battery from the free **Chronos** app (ADR-0005).
Flash it from `firmware/`:

```bash
pio run -e crowpanel21-chronos -t upload
```

**Phone setup (Android, once):**

1. Install **Chronos** (by fbiego) from the Play Store, open it and allow what it asks for.
2. **Notification access:** Android Settings → Apps → Special app access → Notification
   access → **Chronos** → on. Music and navigation need it: without it, Music shows "No
   media" and navigation never arrives.
3. **Phone permission:** Settings → Apps → Chronos → Permissions → **Phone** (and Call logs
   or Contacts, if listed) → Allow. Caller ID needs it.
4. **Battery:** Settings → Apps → Chronos → Battery → **Unrestricted**, so Android doesn't
   stop it in the background.
5. In Chronos's settings, turn on navigation, music control and phone battery if they're
   listed.

**Pairing (once per phone):**

1. **Don't** pair "sarathi" from Android's Bluetooth settings. In Chronos: **Watches → Pair
   New Devices → Search → sarathi**.
2. The phone and the dash both show a 6-digit code. If they match, turn the knob to
   **Pair** and click (focus starts on Cancel), and accept on the phone.
3. From then on, the phone reconnects by itself without asking. Nothing is accepted from a
   phone that isn't paired.

**Using it:**

- **Navigation:** start navigating in **Google Maps**. The dash shows Google's turn icon,
  the distance and the instruction, as the phone formats them (its language and units).
  There's no map in this mode. **Settings is locked while navigating**: there's no speed
  source, so navigating stands in for "moving".
- **Calls:** the caller's name, and **Dismiss** to hide the call on the dash. Answer or
  reject on the helmet intercom or the phone; Chronos can't do it from the dash.
- **Music:** title and artist, play/pause, next and previous. No track time.
- **Forget phones:** Settings → Forget phones deletes every pairing (for example before
  giving the device away). Also forget "sarathi" in the phone's Bluetooth settings, then
  pair again from Chronos.
- **Software updates:** BLE pauses while an update or Wi-Fi setup runs, so Chronos shows the
  watch as disconnected; it reconnects a few seconds after.
- **Reconnecting after a restart** can take up to about 20 s: Android throttles background
  scanning. Opening the Chronos app usually makes it connect at once.

### Phone link: protocol v1 and the fake phone (Phase 4)

The `crowpanel21-phone` build talks protocol v1 (`docs/protocol/protocol.md`, ADR-0007) over
its own BLE service. Until the sārathi app speaks it, a computer with Bluetooth plays the
phone: `tools/ble_client.py` replays the demo ride over BLE.

```bash
cd firmware && pio run -e crowpanel21-phone -t upload && cd ..
source .venv/bin/activate
python tools/ble_client.py scan                  # is the dash advertising?
python tools/ble_client.py ride                  # pair if needed, then replay the demo ride
python tools/ble_client.py ride --backend osm --speed 4 --fallback-at 30
python tools/ble_client.py forget                # remove the pairing on this computer
```

- **Pairing (once per computer):** the dash shows a 6-digit code, and so does the computer.
  On a GNOME desktop, keep **Settings → Bluetooth** open while pairing: GNOME's pairing
  dialog only works while that panel is open (otherwise BlueZ logs "No agent available" and
  pairing fails). Without a desktop, add `--agent` to confirm the code in the terminal. If
  the codes match, turn the knob to **Pair** and click, then confirm on the computer.
  Nothing moves over the link until both have.
- **One connection at a time:** if the computer's Bluetooth settings stay connected to the
  dash (it shows "Waiting for the app"), the tool can't find it; disconnect it there, or with
  `bluetoothctl disconnect <address>`.
- **After Forget phones on the dash,** run `forget` too: the computer otherwise keeps trying
  its old keys, which the dash refuses.
- **What it sends:** `hello`, then time, battery, the ride frame, the backend and its
  attribution, the route and traffic, then `nav` every second and `pose` five times a second,
  the call at 60 s of ride time, and a playlist. Accept, reject and the music controls on the
  dash come back to it and change what it sends.
- **Options:** `--backend google|mappls|osm|offline` (the name and attribution the dash
  shows when stopped), `--speed N` (replay faster), `--fallback-at S` (switch backend at S
  seconds of ride time: the dash shows "Now: ..." for 10 s), `--address` (pick a device).
- **Needs:** BlueZ on Linux (or macOS, Windows), and `bleak` and `dbus-fast` from
  `tools/requirements.txt`.

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

**Chronos mode.** Add `--chronos` to any of these (`--tour`, `--gallery`, or a plain run) to
replace the demo ride with a pretend phone running the Chronos app. It writes real Chronos
packets through the same `sarathi_link` receiver and bridge as the device: arrow-mode
navigation with the phone's icon, a call with only Dismiss, music without time, a pairing
request, and the phone disconnecting. With `--gallery`, the shots are `<theme>_c<n>_*.bmp`.

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
| `firmware` | **native tests** (include `sarathi_proto` against `docs/protocol/vectors`); **build crowpanel21** (also builds `crowpanel21-chronos` and `crowpanel21-phone`; caches `~/.platformio`); **build simulator**; **OTA manifest vectors** (also the protocol vectors) | `pio test -e native`; `pio run -e crowpanel21`, `-e crowpanel21-chronos` and `-e crowpanel21-phone`; `pio run -e sim`; `python tools/ota_manifest.py check-vectors docs/ota/vectors` and `python tools/protocol_vectors.py check-vectors docs/protocol/vectors` |
| `pre-commit` | all hooks on all files | `pre-commit run --all-files` |
| `android` | lint, unit tests, debug APK, ktlint, detekt | see [Android](#android) |

The **build crowpanel21** job also fails if either env's `firmware.bin` exceeds 90 % of its
6 MB OTA slot (ADR-0004), and writes the size to the run summary. Check locally after a build, from
`firmware/`: `python ../tools/check_image_size.py .pio/build/crowpanel21/firmware.bin
partitions.csv`.

Each `firmware` run uploads an artifact `firmware-<short sha>` with `firmware.bin` (to flash)
and `firmware.elf` (to decode crash backtraces), kept for 30 days. Find it on the run's
summary page under **Artifacts**.

CI uses PlatformIO 6.2.0 and the pre-commit version in `tools/requirements.txt`, matching
the local setup. Actions are pinned to commit SHAs. The `actionlint` hook checks workflow
files before you push.

## Making a release

Firmware releases are built, signed and published by `.github/workflows/release.yml`
(ADR-0004) when you push a version tag. Nothing is signed on a laptop.

```bash
git switch main && git pull
git tag -a v0.3.0 -m "sārathi v0.3.0 — <summary>"
git push origin v0.3.0
```

| Tag | Result |
|---|---|
| `vMAJOR.MINOR.PATCH` (MAJOR 0-999, MINOR and PATCH 0-99) | A release. It becomes GitHub's "latest", so devices offer it as an update. |
| `vMAJOR.MINOR.PATCH-rc.N` | A **pre-release**. It's never "latest", so riders never see it. Test firmware can point at its manifest URL (`-DSARATHI_OTA_MANIFEST_URL=...`). |

**What the workflow does:**

1. **Build job (no secrets):** reads the version and `seq` from the tag. It runs the native
   tests and the OTA vector check, then builds with `SARATHI_VERSION` / `SARATHI_SEQ` set (a
   mismatch fails the build) and checks the image size. The published image is the
   **Chronos build** (`crowpanel21-chronos`, ADR-0005); the demo build is compiled too, as a
   check, but not published.
2. **Publish job:** runs in the protected `release` environment, which only `v*` tags can use
   and which is the only place `OTA_SIGNING_KEY` exists. It signs `manifest.jws` with that key,
   then verifies the manifest against the committed public key exactly as a device would. A
   wrong key fails here.
3. **Release:** publishes the `.bin`, `manifest.jws`, the `.elf` (for decoding crash
   backtraces with the exception decoder) and `SHA256SUMS`, with generated release notes.

A device only installs a release whose manifest verifies, is for its board, and is newer than
what it runs. Local builds are `0.0.0-dev` (seq 0), so any release is newer.

## Android

The companion app lives in `android/`: Kotlin, Jetpack Compose, Gradle (via the wrapper;
no Gradle install needed). Modules:

| Module | Kind | Purpose |
|---|---|---|
| `:app` | Android application | Setup and the ride link (Compose, Material 3): companion pairing, the foreground service, permissions (ADR-0010) |
| `:core:protocol` | Kotlin/JVM library, no dependencies | Protocol v1 framing and message codecs, tested against `docs/protocol/vectors/` |
| `:core:ble` | Android library | The protocol session (JVM-tested) and the Bluetooth transport to the dash |
| `:feature:media`, `:feature:calls` | Android library | Music (media sessions) and calls (call state, TelecomManager, call notifications) on the dash |

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
./gradlew installDebug                           # install on the connected phone (adb)
```

### The phone (wireless debugging)

Nothing to install on the computer: `adb` is in the SDK's `platform-tools`.

1. On the phone: Settings, About phone, tap **Build number** 7 times (Developer options).
2. Developer options, **Wireless debugging**: on, on the same Wi-Fi as the computer.
3. **Pair device with pairing code**, then on the computer:
   ```bash
   ~/Android/Sdk/platform-tools/adb pair <ip:port from the pairing popup> <6-digit code>
   ~/Android/Sdk/platform-tools/adb devices    # the phone appears by itself (mDNS)
   ```
4. Install with `./gradlew installDebug`, or `adb install -r app/build/outputs/apk/debug/app-debug.apk`.

Useful while testing the ride link (ADR-0010):

```bash
adb logcat -v time 'RideLink:V' 'BluetoothGatt:V' '*:S'   # the app's link states and sends
adb shell dumpsys battery unplug && adb shell dumpsys deviceidle force-idle   # deep Doze (screen off)
adb shell dumpsys deviceidle unforce && adb shell dumpsys battery reset       # back to normal
adb shell dumpsys companiondevice                          # the association and presence
```

**First use:** the app asks for Nearby devices, notifications, location, location all the
time, the battery exemption, notification access (music and calls) and the Phone permission
(calls), one at a time; then **Find the dash** (the dash on, nearby):
pick sarathi, compare the codes, and Pair on the dash with the knob. An app installed from a
downloaded APK (not over adb) may find notification access marked "restricted setting":
allow it from the app's info page (⋮ menu, "Allow restricted settings"). A phone that was paired
with the dash's Chronos build keeps working: the dash tells it the services changed.

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

**Chronos: Music shows "No media", or navigation never shows.**
Chronos has no notification access: Android Settings → Apps → Special app access →
Notification access → Chronos → on. Then play a track, or start navigating, again.

**Chronos: "No phone" for a long time after a restart.**
Android scans for watches in the background only now and then. Open the Chronos app; it
reconnects within seconds. If it never does, check the phone's Bluetooth is on and that
"sarathi" is paired in Chronos (not only in Android's Bluetooth settings).

**Chronos: it keeps asking to pair, or nothing arrives after pairing.**
Forget the pairing on both sides (Settings → Forget phones on the dash, and Forget "sarathi"
in Android's Bluetooth settings), then pair again from Chronos and confirm the code with
Pair on the dash. The log line `write(s) refused from a phone that isn't paired` means the
phone is connected but not bonded.

**The screen stays black for a long time after power-up, or draws only partly.**
Fixed in Phase 3: with the board on a laptop's USB port and no serial monitor open, log
writes used to block. If it comes back, check that `Serial.setTxTimeoutMs` is still set in
`src/main.cpp`.

**Black screen, scrolling, wrong colours or touch offset.**
See [`hardware.md`](hardware.md): panel timing, the R/B swap and the touch Y offset are all
documented and verified there. Never hot-plug the display FPC cable while powered.
