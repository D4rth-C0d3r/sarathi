# ADR-0001: BLE only; the phone is the brain

- **Status:** Accepted
- **Date:** 2026-10-20

## Context

sārathi is a motorcycle navigation and dash companion built on the Elecrow CrowPanel 2.1"
Rotary Display (DHE03921D). The constraints that shape the architecture:

- **Radio.** The ESP32-S3 has Bluetooth LE 5 and Wi-Fi, but **no Classic Bluetooth
  (BR/EDR)**. It can't act as an HFP (calls) or A2DP (music) device.
- **Audio already has a home.** Riders use helmet intercoms that pair with the phone over
  Classic Bluetooth for calls and music. A second audio path through the dash would compete
  with the intercom rather than replace it.
- **The device is small.** A 480 px round screen, a knob with a push button, optional touch,
  16 MB flash split into two 6 MB OTA slots, 8 MB PSRAM. No GPS, no cellular modem, no
  storage for map tiles.
- **The phone already knows everything.** On Android, navigation apps post turn-by-turn
  notifications, music apps expose a `MediaSession`, and the telephony stack exposes call
  state and answer/reject. The phone has GPS, data and the user's accounts.
- **Riding is a safety-critical context.** The device must be readable in a one-second
  glance and operable with gloves via the knob. Anything that adds interaction while moving
  is a cost.

## Decision

**The phone is the brain; the device is a BLE display and remote.**

- An Android companion app gathers navigation, call, media, time and battery data, and sends
  it to the device. The device renders it and sends back knob/button actions (accept or
  reject a call, play/pause, skip).
- Phone and device talk over **one custom GATT service**: a write characteristic
  (phone → device) and a notify characteristic (device → phone), MTU ≥ 247, JSON messages.
  The message types are `hello`, `nav`, `nav_end`, `call`, `call_action`, `media`,
  `media_cmd`, `time`, `phone_batt`, `ota_begin`, `ota_chunk` and `ota_end`.
- The **protocol version** is separate from the firmware version and is exchanged in
  `hello`. The protocol is specified in `docs/protocol/` (prose, JSON schema and golden test
  vectors) before either side implements a change.
- **Audio never goes through the device.** Calls and music stay on the phone → intercom
  link.
- Control and OTA writes require a **bonded** link; bonding is confirmed with a knob press.

## Alternatives considered

1. **Device as an audio endpoint (HFP/A2DP).** Not possible on the ESP32-S3; would need a
   different chip and would duplicate the intercom.
2. **Standalone device with its own GPS and maps.** Needs extra hardware, offline map data
   that doesn't fit in flash, and its own routing. The phone already does all of this better.
3. **An existing smartwatch protocol only (ChronosESP32).** Gives notifications and
   media control quickly with an existing app, but we don't control the message set or its
   roadmap. Kept as a Phase 3 stepping stone, not the end state.
4. **Wi-Fi link to the phone.** Higher power, awkward pairing (hotspot or same network),
   and it conflicts with the phone's own Wi-Fi use. Wi-Fi stays for OTA only.

## Consequences

**Good**

- The firmware stays small and focused on display, input, BLE and OTA. Most logic is pure
  C++ in `sarathi_core`/`sarathi_proto` and is unit-tested on the host.
- New features (a different nav app, a new data field) are mostly phone-side changes.
- No audio stack, codecs or Classic BT certification concerns on the device.

**Bad / costs**

- **Android only.** iOS is out of scope; its background and BLE restrictions are far
  stricter and there is no equivalent of notification access for nav apps.
- **Dependence on Android behaviour.** Background execution limits, battery optimisation and
  per-vendor BLE quirks affect reliability; the app must handle reconnects gracefully.
- **Phone battery.** A long ride keeps BLE and the companion service active.
- **BLE bandwidth.** Messages must be compact and rate-limited. BLE OTA (Phase 5) will be
  slow compared with Wi-Fi OTA, which is why Wi-Fi OTA comes first (Phase 2).
- **Two codebases share one contract.** Protocol changes must update `docs/protocol/`
  first, and both the firmware and Android tests run against the same golden vectors.
- **Security surface.** A malicious BLE client could inject UI or firmware; hence bonding
  for control and OTA, and signed firmware images.
