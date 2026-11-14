# ADR-0016: Reliability and diagnostics

- **Status:** Accepted (2026-11-14)
- **Date:** 2026-11-14
- **Related:**
  - ADR-0004 (rollback and the boot self-check)
  - ADR-0007 (protocol)
  - ADR-0015 (BLE updates)
  - `core/ride_health.h` (step H)

## Context

A dash on a bike must survive bad power, hangs and crashes, and say what went wrong without a
laptop. Everything stays local: no cloud analytics.

### What the pinned toolchain already has

From the prebuilt `sdkconfig.h` of Arduino-ESP32 3.3.8 / ESP-IDF 5.5.4, so no sdkconfig rebuild
is needed:

| Feature | Setting |
|---|---|
| Task watchdog | `CONFIG_ESP_TASK_WDT_INIT`, timeout **5 s**, **panic** on trigger, idle task on CPU 0 watched |
| Interrupt watchdog | `CONFIG_ESP_INT_WDT` |
| Brownout detector | `CONFIG_ESP_BROWNOUT_DET`, level 7 |
| Core dump | **to flash**, ELF format, CRC32 (`CONFIG_ESP_COREDUMP_ENABLE_TO_FLASH`) |
| Panic | print and reboot at once |

`firmware/partitions.csv` already has a **64 KB `coredump` partition**. No partition change is
needed; partitions can only be changed over USB.

## Decision

### 1. Watchdog

- **Watched tasks:** the UI task (Arduino's loop) and the knob task subscribe to the task
  watchdog (5 s) and feed it from their loops. NimBLE's host task stays under ESP-IDF's own
  supervision.
- **Long operations:** any operation that may legitimately take longer than 5 s must yield or
  feed: the OTA install's flash writes, and the first map chunk decode.

### 2. Reset reasons and the crash counter

- **Recording:** at boot the firmware records `esp_reset_reason()` in a small ring in RTC memory
  (`RTC_NOINIT`). That memory survives software resets, panics and watchdog resets. A power-off
  or brownout clears it, which is right: those aren't crashes.
- **What counts as a crash:** panic, task or interrupt watchdog, and other abnormal resets.
- **What doesn't:** power-on, brownout, and the deliberate restart after an update.
- **The counter:** consecutive crashes, each within **2 minutes** of boot. **Three in a row**
  boots **safe mode**. A boot that runs 10 minutes clears it.
- **Brownouts:** counted separately, since they're a power-supply problem (ADR-0017). A crank dip
  when the engine starts is the typical cause. They're shown in diagnostics and never trigger
  safe mode.

### 3. Safe mode

- **What runs:** the board, the knob, the BLE link and pairing, the clock, the diagnostics pull
  and BLE updates (ADR-0015).
- **What doesn't:** the map renderer, chunks, Wi-Fi and theme ornaments. A plain screen says
  "Safe mode: send a bug report from the app".
- **Leaving:** a knob long press ("Start normally"), or a successful update, or 10 minutes of
  safe running followed by a power cycle.
- **Rollback stays the bootloader's job:** for a freshly updated, unconfirmed image (ADR-0004),
  the bootloader rolls back before safe mode is ever reached. Safe mode is for confirmed images
  that crash, for example on unusual data.

### 4. Core dumps

- **Writing:** on panic, ESP-IDF writes an ELF core dump to the `coredump` partition (64 KB:
  enough for task stacks and registers; heap and PSRAM aren't included).
- **Keeping:** the dump stays there until the app has pulled it and acknowledged it, then the
  firmware erases it. A newer crash overwrites an unpulled dump: the newest is the most useful.
- **Panel side effect:** the erase is a flash write. It's done with the backlight off, right
  after the transfer.
- **Decoding:** needs the matching `firmware.elf`. CI publishes it with each release (ADR-0019).
  A dump is decoded on a computer with `esp-coredump` and that ELF; the app doesn't decode.

### 5. Device log

- **The ring:** the firmware keeps its own log lines (everything sent to `ESP_LOGx` through our
  logger) in a **64 KB ring in PSRAM**. The ring has the reset reasons, the health counters
  (`RideHealth`: temperature, link drops, stale data) and versions at the top.
- **Lifetime:** it survives a software reset, not a power-off. Before an intentional restart, the
  last 4 KB are copied into the RTC memory as well.
- **Logging stays non-blocking** (CLAUDE.md): the ring is written in memory only.

### 6. Pulled to the app over BLE

- **Messages:** `diag_get` (phone → device) asks for the log or the core dump. The device streams
  it as binary frames with the existing flow control and ends with its SHA-256. `diag_ack` lets
  the device erase the dump.
- **Protocol:** a protocol minor version; spec first.
- **When:** only while stopped. Pulls are small: 64 KB takes about 3–8 s.

### 7. The app's diagnostics screen and bug-report bundle

- **The screen:**
  - versions (app, firmware, protocol)
  - link state and recent drops
  - the dash's health rows
  - crash and brownout counts
  - "Get logs from the dash"
  - "Make a bug report"
- **The bundle** is a `.zip` made on the phone, holding:
  - `device.txt`: versions, reset history, counters, health
  - `device-log.txt`: the ring
  - `coredump.elf`, if any
  - `app-log.txt`: the app's own logcat lines, from its tags only
  - `README.txt`: what each file is
- **Privacy:** everything stays local. Nothing is uploaded, and there's no analytics or crash
  reporting service.
  - **Shared only by the user:** the bundle leaves the phone only through Android's share sheet,
    where the user picks the destination.
  - **Reviewed first:** before sharing, the screen lists what's inside, and each part can be
    left out.
  - **Positions removed:** the app log is filtered. Latitude and longitude patterns and place names
    from ride logs are replaced with `[removed]`, since `Ride` lines name destinations.

## Consequences

- **Firmware modules:** `sarathi_board` (reset reason, watchdog subscription), `sarathi_core`
  (the crash-counter logic, as pure logic with native tests) and `sarathi_ui` (the safe-mode
  screen).
- **App modules:** a new `:feature:diagnostics`.
- **Protocol:** additions for `diag_get`, the data stream and `diag_ack`, spec first.
- **On-device tests:**
  - a forced panic (debug-only trigger), then the dump pulled and decoded
  - a watchdog hang (debug build)
  - three crashes leading to safe mode
  - a brownout counted, not crashed
  - the bundle shared through the share sheet
