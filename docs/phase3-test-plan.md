# Phase 3 test plan: the Chronos phone link (on the device)

What the `crowpanel21-chronos` build must do with a real phone (ADR-0005). Run on the board
with an Android phone set up as in `docs/building.md` ("Phone link: the Chronos app"),
record the result and date here, and re-run when `sarathi_link`, the screens or the radio
policy change.

## Setup

- **Device:** CrowPanel 2.1" with `pio run -e crowpanel21-chronos -t upload`.
- **Phone:** Android with Chronos, notification access, phone permission and unrestricted
  battery. Google Maps for navigation; any music player.
- **Serial log** (115200), to confirm what the dash shows. `[link]` lines report the
  connection, MTU, link security, pairing and any dropped packets. Pairing codes are never
  logged.
- **What the native tests already cover** (`test_chronos`, `test_radio_policy`,
  `test_app_model`): packet decoding against packets recorded from the app (caller
  redacted), malformed and oversized packets, cut-short packets, capabilities, the
  settings lock without speed, pairing commands and the prompt timeout. The cases below
  are what only a phone can show.

## Cases

| # | Case | How | Expected | Result |
|---|---|---|---|---|
| 1 | Pair a new phone | Forget phones on the dash and "sarathi" on the phone; pair from Chronos | Same 6-digit code on both; focus on Cancel; Pair + phone accept bonds (`encrypted 1, bonded 1, authenticated 1`) | **Pass**, 2026-10-24 |
| 2 | Refuse pairing | As 1, but click Cancel | Pairing fails; nothing from the phone is accepted (`write(s) refused from a phone that isn't paired`); Chronos may ask again | **Pass**, 2026-10-24 |
| 3 | Paired phone reconnects | Phone Bluetooth off and on, or restart the board | Reconnects with no prompt; everything works | **Pass**, 2026-10-24 |
| 4 | Forget phones | Settings → Forget phones (1), with navigation ended | Row says "No phones paired"; the phone disconnects and must pair again | **Pass**, 2026-10-24 |
| 5 | Navigation | Navigate in Google Maps | Google's icon at 3× in the theme colour, the phone's distance, the instruction on up to two lines, ETA; no map | **Pass**, 2026-10-24 |
| 6 | Navigation after a reconnect | As 5, then phone Bluetooth off and on a few times | Navigation is back within seconds each time; log shows `MTU 517` at connect and no dropped packets | **Pass**, 2026-10-24, after the device started the MTU exchange itself (before: MTU stayed 23 for minutes and nav was missing) |
| 7 | Settings lock without speed | While navigating, double-click through the screens | Settings is skipped; it comes back when navigation ends | **Pass**, 2026-10-24 |
| 8 | Incoming call | Call from a saved contact; let it ring out | Call screen with the contact's name and a single Dismiss; back to the previous screen when it stops ringing | **Pass**, 2026-10-24 |
| 9 | Dismiss | As 8, click Dismiss while it rings | The dash goes back; the phone keeps ringing | **Pass**, 2026-10-24 |
| 10 | Music | Play, pause, next, previous from the dash | Title and artist; the phone's player follows; no time or progress ring | **Pass**, 2026-10-24 (needs notification access; without it, "No media") |
| 11 | Phone gone | Phone Bluetooth off | Nav says "Phone not connected"; no stale nav or call left showing | **Pass**, 2026-10-24 |
| 12 | Update check with BLE | Settings → Check for update | Chronos disconnects (BLE off); the check runs; BLE back about 3 s after Wi-Fi is off; Chronos reconnects | **Pass**, 2026-10-24 (lowest internal heap 75 KB) |
| 13 | Power-up on a laptop, no monitor | Unplug and replug USB with no serial monitor open | Full screen within about 2 s; Chronos connects when it finds the board | **Pass**, 2026-10-24 (before the fix: black for about a minute, then half drawn) |
| 14 | Knob on the music screen | Turn to Prev/Next and click, repeatedly | Screen stays on (no instant sleep) | **Pass**, 2026-10-24 |
| 15 | Double-click | Quick double-clicks on every screen | Switches screen every time | **Pass**, 2026-10-24 |
| 16 | Clock | Connect; compare the dash's clock with the phone's | Same time, kept between syncs | **Pass** in the log (recorded time packet decoded to 19:26), 2026-10-24 |
| 17 | OTA to a Chronos release, boot self-check with BLE | Publish a `v0.4.0-rc.N` pre-release; install it from test firmware (`docs/ota-test-plan.md`) | Installs; the image confirms only after BLE is up; the OTA test plan's cases still pass with BLE stopping and restarting around them | **Not run yet**: needs the first Chronos pre-release |
| 18 | On the bike | A real ride with navigation, a call and music, phone in a pocket or mount | Readable at a glance; nav keeps up; reconnects after brief drops | **Not run yet** |

## Results log

- **2026-10-24:** cases 1 to 16 passed on the development board. Issues found and fixed on
  the way:
  - **Missed double-clicks:** the button is now read by the knob task.
  - **Screen sleeping right after a knob turn:** idle time is now signed.
  - **Slow, half-drawn power-up:** log writes no longer block on USB.
  - **Navigation missing after a reconnect:** the device now starts the MTU exchange.
  - **USB disconnects in the laptop's kernel log** (`error -71`) during one session were the
    cable; the board kept running.

  Case 17 waits for the first Chronos pre-release, case 18 for a ride.
