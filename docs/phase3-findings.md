# Phase 3 findings: ChronosESP32

What the ChronosESP32 library can and can't do for sārathi, checked against its source before
any adapter code was written. Source read: `fbiego/chronos-esp32` tag `1.9.1`
(`src/ChronosESP32.{h,cpp}`, `api.md`, `examples/`). Build and memory figures come from a
throwaway build of our firmware with the library linked in, run on the CrowPanel on
2026-10-24.

## Summary

| Need | What Chronos gives | Verdict |
|---|---|---|
| Navigation | Phone-formatted **text** (next distance, instruction/street, remaining distance, duration, ETA) and a **48×48 1-bpp icon**. No maneuver code, no geometry | Arrow mode only; draw the phone's icon |
| Incoming call | Caller text when ringing starts; "stopped" when it stops ringing | Show the caller; **no accept or reject** |
| Music | Title, artist, playing or paused; play, pause, toggle, next, previous, volume | Supported (no position or duration) |
| Time | Phone sets the ESP32 clock (local wall time) | Supported |
| Phone battery | Level and charging state | Supported |
| Speed | Only with OsmAnd (a text field); not with Google Maps | No speed source: need a stand-in for the moving lock |
| BLE security | None: no pairing, bonding or encryption | Known gap, unless the experiment in "Security" works |
| Toolchain | Builds on our pinned core 3.3.8 with NimBLE-Arduino 2.5.1 | OK |
| Size | +260 KB flash; image 2,015,712 B = 32 % of a 6 MB slot | OK (CI limit 90 %) |
| Internal RAM | NimBLE + Chronos take **72 KB** of internal heap | Too much to run beside a Wi-Fi update: stop BLE during updates |

## Dependencies and licences

| Library | Version | Licence | Notes |
|---|---|---|---|
| `fbiego/ChronosESP32` | 1.9.1 | MIT | Latest release |
| `h2zero/NimBLE-Arduino` | 2.5.1 | Apache-2.0 | Chronos asks for `^2.1.0`; 2.5.1 is the latest 2.x |
| `fbiego/ESP32Time` | 2.0.6 | MIT | Chronos inherits from it for the clock |

All three are compatible with our Apache-2.0 licence. They would be pinned to these exact
versions in a separate PlatformIO env only, so the demo build, the simulator and the native
tests don't change.

**Build on core 3.3.8:** `pio run` succeeds. The three libraries give one harmless warning
(Chronos calls the deprecated no-op `NimBLEService::start()`).
Flash grows from 1,754,718 B to 2,015,310 B (+260 KB) and static RAM from 58,384 B to
65,308 B (+6.9 KB).

## Navigation

Chronos forwards what the phone's navigation notification (Google Maps) shows. Command
`0xEF` carries the text and `0xEE` the icon, in three 96-byte chunks.

| Field | Meaning (from the source and `api.md`) | Example form |
|---|---|---|
| `active` | A navigation notification is present | |
| `isNavigation` | It's turn-by-turn, not general info | |
| `title` | Distance to the next maneuver, or a title | "200 m" |
| `directions` | The instruction or current street | "Turn left onto MG Road" |
| `distance` | Remaining distance to the destination | "12 km" |
| `duration` | Remaining time | "25 min" |
| `eta` | Arrival time | "18:42" |
| `speed` | Speed; only filled by OsmAnd | |
| `icon` | 48×48, 1 bit per pixel, MSB first, row-major (288 B) | |
| `iconCRC` | Changes when the icon changes | |

- The examples above show the form; exact strings depend on the phone's language and units,
  and are confirmed during step 2's recording.
- All values are **preformatted strings** in the phone's language and units. There is no
  numeric distance and no maneuver enum.
- The library marks navigation inactive on disconnect (`onDisconnect`), so a BLE drop is
  visible rather than frozen.
- `active` with `isNavigation == false` also covers the library's own placeholder states
  ("Start navigation on Google maps", "Check Chronos app settings"). The adapter treats only
  `active && isNavigation` as navigating.

**Consequences for our model:** `NavState` holds `distance_m` (number) and `eta_s` (seconds).
Chronos can't fill those without parsing locale-specific strings ("1,2 km", "0.3 mi",
"500 ft", other scripts), which would go wrong silently. So the model needs a **text path**:
the next distance and ETA as given by the phone, shown as is. The phone's unit setting wins
over our Units setting in this mode, and the docs will say so. This is a `sarathi_core` model
change, not a protocol change.

### Decision: draw the phone's icon, don't map it to our arrows

We'll draw Chronos's 48×48 icon, upscaled with whole-pixel scaling and tinted with the
theme's maneuver colour. We won't classify it into our `ManeuverType` arrows.

- **The icon is the only maneuver information Chronos sends.** Mapping means guessing the
  maneuver from pixels.
- **Google Maps has many more maneuvers than our 12 types:** ramps, forks, keep left/right,
  merges, and roundabouts with exit numbers drawn in. A classifier would show a wrong arrow
  for anything it hasn't seen.
- **A confident wrong arrow is worse than a plainer right one.** The phone's own icon always
  means what the phone means.
- **The cost is small and contained:**
  - Themes can tint the icon but not restyle its shape.
  - It's pixel art: at 4× it's 192 px, with steps of 4 px.
  - The upscaling is pure logic in `sarathi_core`, testable natively; the UI only shows the
    result as an alpha image.
- **In Phase 4,** our own app sends real maneuver types, and the themed arrows return.

## Calls

- **Incoming:** command `0x72` with `icon == 0x01` calls `ringerCallback(caller, true)`.
  `caller` is the text the phone shows: a contact name or the number.
- **Stopped:** `icon == 0x02` calls `ringerCallback(caller, false)` when the phone stops
  ringing. That can mean answered, rejected or missed, and the library can't tell which.
- **No call control.** The library has no command to accept, reject or silence a call. Its
  outgoing commands are music, volume, find phone, camera, battery and health. We won't send
  undocumented raw commands.
- **So in Chronos mode:**
  - The call screen shows the caller with a single **Dismiss** action, which hides the screen
    on the dash only. The phone keeps ringing.
  - The rider answers on the helmet intercom, as ADR-0001 intends.
  - There's no "active call" phase, because we can't tell an answered call from a missed one.

## Music

- **Track info:** command `0x9D`, sub-commands `0x80` (state, colours, app name), `0x81`
  (title) and `0x82` (artist). `state` is 0 for paused and 1 for playing.
- **Not sent:** position and duration.
- **Controls:** `musicControl()` sends `MUSIC_PLAY`, `MUSIC_PAUSE`, `MUSIC_TOGGLE`,
  `MUSIC_NEXT`, `MUSIC_PREVIOUS`, `VOLUME_UP`, `VOLUME_DOWN` and `VOLUME_MUTE`.
- **Blocking:** every `sendCommand()` blocks for **200 ms** (`vTaskDelay`) after each notify,
  so music commands must never be sent from the UI task (see "Threading").

## Time and phone battery

- **Time:** command `0x93` calls `ESP32Time::setTime(...)`, which sets the system clock to
  the phone's local wall time. The adapter reads it back for the model's clock.
- **Phone battery:** command `0x91` gives level (0–100) and charging, via `CF_PBAT`. The
  library asks the phone for battery updates 3 s after it subscribes (`setNotifyBattery`).

## Threading and memory

- **Library work runs on NimBLE's host task.** All parsing happens inside the RX
  characteristic's `onWrite`, which builds Arduino `String`s (heap allocation) and then calls
  our callbacks there.
  - The allocation is inside the library, and we can't avoid it without forking.
  - Our callbacks follow our BLE rules: they copy the values into fixed buffers under a short
    lock and return.
  - `Poll()` on the UI task applies them to `AppModel`.
- **`loop()` and `musicControl()` block** for hundreds of milliseconds (several 200 ms
  delays), so they run on a small, low-priority **link task**. The UI posts music commands to
  it through a queue.
- **Constructor bug:** `ChronosESP32(String name, ChronosScreen)` calls `ChronosESP32();` in
  its body. That builds a whole temporary object (11.7 KB) on the caller's stack and throws
  it away. On the device this overflowed `loopTask`'s stack at boot.
  - The adapter uses the default constructor, then `setName()` and `setScreen()`.
- **Uninitialised fields:** the default constructor leaves some members (`_inited`,
  `_subscribed`, …) uninitialised.
  - The adapter creates the object in zeroed PSRAM (`heap_caps_calloc` plus placement new),
    which also keeps its 11.7 KB out of internal RAM.
- **Internal RAM cost:** with the app running, free internal heap went from **169 KB to
  97 KB** when NimBLE started. A Wi-Fi update needs about 100 KB of internal RAM
  (Phase 2: 191 KB free at boot, 91 KB during a download). It can't run with BLE up.
  - So **BLE stops while a software update or Wi-Fi setup runs**, and restarts afterwards
    (or on the rollback path, after the restart).
  - Both are stopped-only actions, and the dash says the phone link is paused.
  - Step 2 will also try NimBLE's memory settings (one connection, fewer buffers) and report
    the saving. It won't rely on them.

## Security

### No BLE security in the library

- `begin()` creates a GATT service with a plain `WRITE | WRITE_NR` characteristic. It never
  calls `NimBLEDevice::setSecurityAuth`, `setSecurityIOCap` or `startSecurity`.
- Any phone or laptop in range can connect and write without pairing.
- The library's setup instructions say *not* to pair the board in the phone's Bluetooth
  settings, so the Chronos app doesn't expect bonding.

### Memory-safety bugs reachable without pairing

`onWrite` and `dataReceived` trust lengths and indices from the packet:

| Where | What | Effect |
|---|---|---|
| `onWrite`, continuation packet | Writes at `20 + seq·19` with `seq` up to 255, into a 512-byte buffer | Write up to ~4.4 KB past the buffer |
| `onWrite`, first packet | Copies up to the MTU (517 B) into 512 B | Write up to 5 B past the buffer |
| `dataReceived` | Declared length (up to 65,538) bounds the read loops | Reads past the buffer |
| `0xEE` nav icon | Chunk index unchecked (0–255) into the 288-byte icon | Write past the icon |
| `0xA8` QR link | Index unchecked into 9 `String`s | Write past the array |
| `0xA2`/`0xA3` contact | Index 255 into a 255-entry array | Write one past the end |

The Chronos app wouldn't send these, but anyone nearby could. For a device on a motorcycle
that's a real risk: a crash, or worse, while riding.

- **Mitigation without forking:**
  - After `begin()`, the adapter replaces the RX characteristic's callbacks with a **guard**.
  - The guard checks each packet before passing it on:
    - lengths within 512 B
    - continuation offsets in range
    - only the commands we use (time, battery, ringer, music, nav, nav icon with a chunk
      index ≤ 2, transfer mode, sync)
    - the icon chunk index
  - Accepted packets go to the library through its public base-class interface
    (`NimBLECharacteristicCallbacks::onWrite`). That's legal C++ and needs no edit to the
    library.
  - The guard is pure logic over bytes, so it gets native tests, including the malformed
    packets above.
- **Reporting upstream:** these are worth reporting to the library. With your agreement, I'd
  draft the report for you to file; I won't post it myself.

### Bonding with knob confirmation: possible, untested

NimBLE supports what we need: bonding, MITM protection, numeric comparison with a yes/no
display, and starting security from the device. The adapter could also take over the
server callbacks to show the 6-digit code on the dash and confirm it with a knob press. What
nobody knows is whether the **Chronos app** copes with a bonded, encrypted link.

**Proposal:** a time-boxed experiment in step 2 on your phone.
- **If it works:** require it, so the rule holds.
- **If the app breaks:** record it as a known Phase 3 gap in `SECURITY.md` and ADR-0005, as
  you specified, with the guard as the mitigation.
- **Either way:**
  - OTA stays on the signed Wi-Fi path only, and nothing in Chronos mode can trigger an
    update.
  - The phone link only carries display data and music commands.

## Moving lock

No speed source exists with Google Maps (Chronos only passes OsmAnd's speed text). As
approved, in a backend without speed, **Settings locks while navigation is active** and
unlocks when navigation ends or the phone disconnects. Software update is under Settings, so
it's covered too.

## Boot self-check

The Chronos build makes "BLE up" a real condition (`BootHealth::ble_ready`). The image is
confirmed only after advertising has started.

## Still to confirm with the phone (step 2)

- The exact strings Google Maps produces for each field, in your language and units. These
  will be recorded as fixtures for the native tests, with names and numbers redacted.
- What `caller` contains for a contact and for an unknown number.
- How often navigation updates arrive (affects how we show stale data while stopped).
- The bonding experiment above.
