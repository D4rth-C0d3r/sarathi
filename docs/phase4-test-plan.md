# Phase 4 test plan

On-device checks for each Phase 4 step, run on the CrowPanel with the noted build. Native and
JVM tests cover the logic; these cover what only the hardware shows.

## Step C: our own phone link (`crowpanel21-phone`, `tools/ble_client.py`)

Flash with `pio run -e crowpanel21-phone -t upload` (from `firmware/`), keep
`pio device monitor` open, and run the fake phone from the repository root in the `.venv`.

| # | Check | How | Pass |
|---|---|---|---|
| C1 | Advertising | Boot; `python tools/ble_client.py scan` | The dash is listed; Nav shows "No phone"; Settings shows "Phone: not connected" |
| C2 | Pairing, confirmed | `ride`; when both show a code, turn to **Pair**, click, confirm on the computer | Same code on both; log: `link security: encrypted 1, bonded 1, authenticated 1`; "Forget phones (1)" |
| C3 | Pairing, refused | `forget`, then `ride` again and click **Cancel** on the dash | The computer reports a failed pairing; nothing reaches the dash; Nav shows "No phone" again |
| C4 | Reconnecting | Stop the tool (Ctrl-C), run `ride` again | No new code; connects at once |
| C5 | The ride | Let `ride` run | Nav map with the route, the turn, distance and ETA; the map turns at the corners; settings lock while moving and unlock at the two stops |
| C6 | Attribution, stopped | At the first stop (traffic light, 10 s) | Backend name and attribution in the status spot, small, inside the circle, in every theme (switch theme in Settings at the stop) |
| C7 | Backend switch | `ride --fallback-at 20` | "Now: Mappls" for 10 s while riding |
| C8 | Call | At 60 s ride time, Accept on the dash; then again with Reject (restart the tool) | The tool logs `call_action: accept` and the dash shows the active call; reject ends it |
| C9 | Music | On Music: play/pause, next, previous | The tool logs `media_cmd: ...`; title and time follow |
| C10 | Stale data | Pause the tool (Ctrl-Z) for 15 s, then `fg` | "Position stale Ns" after 3 s, "Navigation stale" after 10 s; both clear when it resumes |
| C11 | Disconnect | Ctrl-C mid-ride | Within a few seconds: "No phone"; nav and call cleared, not frozen |
| C12 | Wi-Fi pauses BLE | Settings → Check for update while connected | Log: `BLE stopped for Wi-Fi`; Nav shows "Bluetooth paused"; after it, the tool can reconnect |
| C13 | Forget phones | Settings → Forget phones | The tool disconnects; `ride` asks for a code again |
| C14 | Second phone | Scan with another BLE central (for example nRF Connect) while the tool is connected | The dash isn't listed: it stops advertising while a phone is connected. (If two connect at the same instant, the second is turned away: `extra phone(s) turned away`.) The ride carries on |

### Results, 2026-10-31 (the fake phone on the maintainer's laptop)

All of C1-C14 pass on the device, after the fixes the first run found:

- **Pairing through BlueZ** failed at first: the dash disconnected on its own failed security
  request before the laptop could pair, and GNOME's agent only answers with Settings →
  Bluetooth open. Fixed in the firmware and the tool; documented in `docs/building.md`.
- **The map redrew five times a second** (every pose), holding the dash at about 5 fps; now
  once a second.
- **"Phone not connected" was cut** to "Phone not..." on the Nav card; now "No phone".
- **"Securing link"** showed while a computer's Bluetooth settings held the secured link
  without the app; now "Waiting for app".
- **An unpaired central can turn notifications on** (NimBLE doesn't protect the CCCD); the
  device still sends nothing before the link is secured. SECURITY.md corrected.

The update check during C12 couldn't reach GitHub from the test network (TLS connect
timeouts); BLE paused and came back as it should.

## Step D: the Android app and the ride link

Phone with the debug app (`docs/building.md`, "The phone"), the dash on the `crowpanel21-phone`
build, `adb logcat -v time 'RideLink:V' 'BluetoothGatt:V' '*:S'` running.

| # | Check | How | Pass |
|---|---|---|---|
| D1 | Setup | Fresh install; go through the steps | One step at a time, each with its reason; every permission granted |
| D2 | Pairing | Find the dash; pick sarathi; compare codes; Pair on the dash | Bonded; the link reaches Ready; the dash shows "No navigation" |
| D3 | Phone paired with the Chronos build earlier | The same, on such a phone | Connects (the dash's Service Changed refreshes Android's cache) |
| D4 | Service | `adb shell dumpsys activity services io.github.d4rthc0d3r.sarathi` | Foreground, `types=0x00000018` (connected device and location) |
| D5 | Deep Doze | Lock the phone; force idle; `dumpsys battery set level 40` | The link stays up; `sent PhoneBattery(level=40...)` while IDLE; location fixes keep coming |
| D6 | Dash restarts, phone locked in Doze | Reset or power-cycle the dash | Reconnects within seconds; battery and time resent |
| D7 | Stop | Stop in the app or the notification | The notification goes; the dash shows "No phone" |
| D8 | Auto-start | Switch on, Stop, lock; dash off 10 min, then on | The link starts by itself |
| D9 | Pocket ride | Ride with the phone locked in a pocket | The link stays up the whole ride; location fixes about once a second outdoors |

### Results, 2026-10-31 (Nothing Phone (1), Android 15)

- **D1, D2, D4–D7 pass.** Connect to Ready takes about 300 ms; after a dash reset in deep Doze
  the phone reconnected in 3 s.
- **D3 failed, then passes after two fixes:** the phone had cached the Chronos build's services.
  The dash now sends Service Changed once per phone per boot, and the app re-reads the
  services if that arrives during setup.
- **D8 fails on this phone:** Android never reported the dash as gone (still "nearby" after 10
  minutes unplugged), so there was no "appeared" to start the link. Deferred: auto-start that
  keeps the link waiting all the time (ADR-0010).
- **D9 not done yet** (indoors the check only saw Wi-Fi and cell fixes, about one every 4 s).

## Step E: calls, music, position and the moving lock

The dash on the `crowpanel21-phone` build (protocol 1.1), the phone with the debug app.

| # | Check | How | Pass |
|---|---|---|---|
| E1 | Setup | Music and Calls steps | Notification access and the Phone permission granted, each with its reason |
| E2 | Music | Play in a music app; play/pause, next, previous on the dash | Title, artist and time on the dash; the controls work |
| E3 | Cellular call, accept | Call the phone from another; Accept on the dash | The caller's name as the phone shows it; the call is answered |
| E4 | Cellular call, reject | Again; Reject on the dash | The call is rejected; Reject during a call hangs up |
| E5 | Internet call | A WhatsApp call; accept and reject on the dash | The name; WhatsApp answers or declines |
| E6 | Position | `adb shell dumpsys location` | The app receives fixes; the service has types `0x18` |
| E7 | Moving lock | Lock card: moving from 2 km/h; walk with the phone, then stand still | Dash settings lock after about 2 s of walking and unlock about 5 s after stopping |
| E8 | Lock off | Turn the switch off (warning shown), walk again | The settings stay unlocked; the card says it's off |

### Results, 2026-10-31 (Nothing Phone (1), Android 15)

- **E1-E6 pass:** music shows and the dash's controls work; calls show the name, and accept and
  reject from the dash work; the app sends `lock` on connect and position (123 fixes in the first
  11 minutes, indoors).
- **The settings reach the dash at once:** every tap in the lock card was sent as `lock`
  within a second.
- **E7, E8 left for a ride.** An indoor walk can't test them: indoors the phone only gets Wi-Fi
  and cell fixes (20-200 m accuracy, one every 5-50 s), so the dash, correctly, marks the speed
  unknown between fixes and never decides "moving". Outdoors, GPS gives a fix a second. The
  dash logs every change (`[link] motion: ...`) and the phone the speed it sends
  (`pose speed ... km/h`), so a ride's logs show how the lock behaved.
