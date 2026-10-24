# ADR-0005: The Chronos app as an interim phone link

- **Status:** Accepted
- **Date:** 2026-10-24
- **Related:** ADR-0001 (BLE only, phone as brain), ADR-0002 (vector route map), ADR-0003
  (themes), ADR-0004 (OTA and boot self-check); findings in `docs/phase3-findings.md`

## Context

Phase 4 brings our own Android app and protocol. Until then the dash has no phone data: the
device build runs the scripted demo ride. Phase 3 is a quick win, a real phone link for
riding now, through the existing Chronos app (fbiego/chronos-esp32, MIT) and its ESP32
library. It must not bend the architecture or the rules, and it must be easy to remove.

The library's source was read before any code was written (`docs/phase3-findings.md`):

- **Navigation** is the phone's text (distance, instruction, remaining time) and a 48×48
  1-bit icon, forwarded from Google Maps' notification. No maneuver type, no geometry, no
  speed.
- **Calls:** the caller when ringing starts and a "stopped ringing" event. No command to
  accept or reject.
- **Music:** title, artist, playing or paused, and play/pause/next/previous. No position.
- **Time and phone battery:** yes.
- **No BLE security,** and its packet parser trusts lengths and indices sent by any device
  in range (writes of up to about 4.4 KB past a 512-byte buffer). Its named constructor
  overflowed our stack at boot. Its sends block for 200 ms.
- **Cost:** about 260 KB of flash and, with NimBLE running, 72 KB of internal RAM.

## Decision

### A data source like the demo ride, chosen at build time

- **New env `crowpanel21-chronos`** (`-DSARATHI_BACKEND_CHRONOS`), with ChronosESP32 1.9.1,
  NimBLE-Arduino 2.5.1 and ESP32Time 2.0.6 pinned in it alone. `crowpanel21` (demo ride),
  the simulator and the native tests are unchanged.
- **All of it lives in `sarathi_link`.** Chronos and NimBLE types never leave
  `chronos_source.cpp`. `main.cpp` picks the source in one `#if` block.
- **Our own receiver, not the library's parser.** Our RX callback replaces the library's and
  only queues each write. `chronos_packets` (pure C++) reassembles writes with every length,
  offset and index checked, and decodes only the commands we use. `chronos_bridge` (pure)
  turns them into app state. Both are native-tested against packets recorded from the app,
  plus the malformed packets the library would have mishandled.
- **Threads:**
  - NimBLE's host task only copies data.
  - A link task runs the library's blocking `loop()` and sends.
  - The UI task decodes and updates the model.
- **The library is used for** GATT setup, advertising, its handshake with the app and music
  commands. It's created with the default constructor, in zeroed PSRAM.

### The UI offers only what the source supports

A `Capabilities` struct in the model says what the source can do: call accept or reject,
media control and progress, route geometry, speed, phone pairing. For Chronos:

- **Nav, arrow mode:**
  - The map is hidden.
  - The phone's icon is drawn at 3×, tinted with the theme's maneuver colour.
  - The phone's distance text is the primary item, with the instruction on two lines and
    the remaining time below.
  - We draw the phone's icon instead of mapping it to our arrows. Google Maps has far more
    maneuvers than our twelve, and a confidently wrong arrow is worse than the phone's own
    picture. Phase 4's own app sends real maneuver types, and the themed arrows return.
- **Distance and time** are shown as the phone formats them, in its language and units.
  Parsing strings like "1,2 km" would go wrong silently. The phone's unit setting wins over
  ours in this mode.
- **Call:** a single neutral **Dismiss**. It hides the call on the dash only; the rider
  answers on the helmet intercom (ADR-0001).
- **Music:** no time or progress ring.
- **No phone:** Nav says "Phone not connected", so stale data is never shown.

### Safety: no speed, so lock while navigating

Chronos gives no speed with Google Maps. Without a speed source, **Settings locks while
navigation is active**, and unlocks when it ends or the phone disconnects. The software
update and Forget phones rows sit under Settings, so they lock too.

### Security: the rule holds

A bonding experiment on the device showed that the Chronos app works over a bonded,
encrypted, MITM-protected link. So:

- **Every phone is asked to secure the link** when it connects.
- **A new phone pairs by numeric comparison.** The dash shows the 6-digit code with Pair and
  Cancel, focus on Cancel; the rider confirms with the knob. It times out after 30 s.
- **Writes are accepted only over an encrypted, bonded and authenticated link,** so a "Just
  Works" bond, which never showed a code, doesn't count.
- **Bonded phones reconnect silently.** Settings → Forget phones deletes every bond.
- **OTA stays on the signed Wi-Fi path (ADR-0004).** Nothing over BLE can start an update.

### Radio: BLE and Wi-Fi take turns

NimBLE (72 KB) and a Wi-Fi update (about 100 KB) don't both fit in internal RAM.

- **BLE stops** while an update or Wi-Fi setup uses Wi-Fi.
- **Wi-Fi jobs wait** until BLE is off.
- **BLE restarts** 3 s after Wi-Fi is off.

The policy is pure logic (`core/radio_policy`), and the boot self-check waits for BLE in
this build.

### What ships

From Phase 3, tagged releases (and so OTA updates) ship the **Chronos build**, the one
that's useful on a bike. The release workflow still builds the demo env as a check, but
doesn't publish it.

## Consequences

- **Riding with the dash works now:** nav arrows and distance, caller ID, music and the
  clock, on the phone the rider already has.
- **Phone setup is needed:** install Chronos, allow notification access (music and nav need
  it) and unrestricted battery, and pair from the app (`docs/building.md`).
- **Known limits until Phase 4:**
  - no map
  - no call control
  - no track position
  - no speed, so Settings locks while navigating
  - the phone's units, not ours
  - Google Maps only for navigation
- **Library issues we work around:** unsafe parser (bypassed), constructor stack overflow
  (avoided), blocking sends (on the link task), no security (ours added). They're worth
  reporting upstream.
- **Phone quirks we handle:**
  - The app writes only the first 20 bytes of a longer packet while the ATT MTU is 23.
  - After bonding, the phone sometimes waited minutes to raise the MTU, which hid
    navigation. The device now starts the MTU exchange on connect.
- **Size:** 2,026,864 B, 32.2 % of a 6 MB slot (CI checks the Chronos env too).

## Exit plan (Phase 4)

When our own app and protocol (`docs/protocol/`) land, they become a new `DataSource` in
`sarathi_link`, with real maneuver types, route windows, call control and the bonded link
kept from this phase. Removing Chronos then means deleting:

- the `crowpanel21-chronos` env and its three libraries
- the `#if` branch in `src/main.cpp`
- `sarathi_link/chronos_*`, `test/test_chronos/` and the simulator's `fake_chronos`

What stays because it's general:
- the capabilities
- arrow mode, which also serves our app before a route window arrives
- the radio policy
- the pairing screen and Forget phones
- the receiver's defensive style
