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
| D10 | Bluetooth off and on | Link connected; turn Bluetooth off in quick settings, wait 10 s, turn it on. **Not over adb:** `svc bluetooth` can wedge the phone's Bluetooth | Home says Bluetooth is off, then the link reconnects by itself, without restarting the app |

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
- **D10 passes since 2026-11-09** (`9eae54a`). Before that fix, the link stayed "Bluetooth is off"
  until the app restarted. Now the dash logs the phone gone when Bluetooth goes off and
  connected again right after it comes back on.

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

## Step F: maps backends, guidance and the ride on the dash

The dash on the `crowpanel21-phone` build, the phone with the debug app and the keys in
`~/sarathi-secrets` (`docs/building.md`). The dash's serial ride log (`[link] ride:` and
`screen:`, step F7) shows what the dash received and showed, so most checks need nobody at the
dash; the phone's side is in logcat (`Ride`, `Links`, `GoogleNav`). Restore the maps settings
and recents after a test that changes them. Every Google route bills one destination per stop.

| # | Check | How | Pass |
|---|---|---|---|
| F1 | Maps settings | Home → Maps settings | Keys present (never shown), usage meters, Mappls caps and the backend choice survive an app restart |
| F2 | Search | "Where to?" on each maps backend (Google, Mappls, OpenStreetMap); a Mappls Pin (`st5t6t`) | Results from the chosen backend, which also says what was skipped; the place's coordinates; recents and saved places; the usage meters count |
| F3 | Shared links | Share a place and a route from Google Maps, a place from Mappls, an OSM and an Organic Maps link, a `geo:` link | sārathi in the share sheet and for `geo:`/map links; the place or the stops in order |
| F4 | Routing | Route to one destination with each backend ("Route with"; offline by failing the online ones) | A route from every backend, with its profile; GraphHopper labelled a car route |
| F5 | Traffic | Mappls route | Traffic by level and a traffic-aware time on the route card |
| F6 | Guidance | "Simulate ride", then "Simulate with a detour" (Mappls); Google's simulator | Steps to "arrived"; a detour reroutes and arrives; Google's guidance runs headless to the destination |
| F7 | Ride on the dash | Route, confirmation screen, **Start the ride**; lock the phone; **End the ride** | Confirmation shows the route, backend, meter and checks; the dash logs frame, backend, route, traffic, nav in that order, nav keeps coming while locked, and "navigation ended" |
| F8.1 | Simulated ride | Confirmation screen → **Simulate ×20**, phone locked | Turn by turn to the end on the dash, no "Position stale", no reroute |
| F8.2 | Recorded ride | **Record for desk tests** (not for Google routes), then **Replay** on Home | The recorded route with three scripted traffic updates 30 s apart; no backend calls |
| F8.3 | Failure drill | Maps settings → **Failure drill** on; simulate with a detour | The dash shows "Now: OSM (Valhalla)", then "Now: OSM offline", with fallback events, route versions 1 and 2, and arrival |
| F9 | Google traffic in guidance | A real ride with Google guidance | `GoogleNav` "traffic:" lines in logcat; traffic on the dash |

### Results, 2026-10-31 and 2026-11-09 (Nothing Phone (1), Android 15)

F1–F8 pass. F9 waits for a real ride.

- **F1, F2 (2026-10-31):** search works on all three backends, including Mappls Pins. Mappls's
  analytics are switched off (ADR-0006).
- **F3:** all 11 recorded links resolve, and so does real sharing from Google Maps and Mappls.
  - Found and fixed: a crash in recents with two unnamed places of the same name.
  - Found and fixed: Mappls's place details return nothing on the free plan, so Pins are looked
    up through autosuggest.
- **F4:** routes from the phone's position to Calangute Beach:

  | Backend | Result |
  |---|---|
  | Google | 31.5 km, 60 min, two-wheeler |
  | Mappls | 30.6 km, 48 min, `biking` |
  | Valhalla | 33.6 km, 75 min, motorcycle |
  | GraphHopper | 33.2 km, 41 min, car (free plan) |
  | OSM offline | 31.2 km, 31 min, about 7 s including the first copy of the tiles |

- **F5:** Mappls traffic came with the route: 23.2 km free, 3.6 km slow, 0.2 km jam and 4.4 km
  unknown, 55 min with traffic.
  - Mappls can't refresh traffic on the free plan, since its answers carry no request ID.
  - Google sent no traffic before guidance.
- **F6:**
  - **Mappls simulated ride:** all 34 steps to "arrived". A 400 m detour rerouted on Mappls and
    arrived; the simulator's off-road detour rerouted 4 times.
  - **Google guidance:** ran headless to the destination with Google's simulator. No traffic
    arrived in 7 minutes, which is why F9 waits for a real ride.
- **F7:**
  - **Confirmation:** Mappls, 31.4 km, 58 min, all four checks yes.
  - **On the dash:** the route arrived as 441 points and 35 markers, in the protocol's order.
    `nav` kept arriving with the phone locked and dozing.
  - **Found and fixed:**
    - Frames of 514 bytes at MTU 517 were refused by Android, so frames are now capped at 512 B.
    - After a dash reboot the route came before the ride frame and was dropped; `frame` is now
      sent first.
    - The first traffic and `nav` could go ahead of the route.
- **F8:**
  - **F8.1:** Mappls ×20 ran to "navigation ended" with the phone locked. The first try put
    fixes 300 m apart and rerouted every 10 s. Simulated fixes are now 15 m apart, and on real
    rides too, reroutes in a row back off from 10 s up to 5 minutes.
  - **F8.2:** passes.
  - **F8.3:** passes, at one Mappls call and no Google destination.
    - Found and fixed: the dash kept naming the old backend after a mid-ride fallback.
    - Found and fixed: the drill skipped Google when the maps backend was Mappls.
- **Usage for all of step F:** 4 of Google's 1,000 destinations this month; Mappls at 30 of 100
  routing calls on 2026-11-09.
- **Deferred:**
  - downloading map data for the route corridor, and drawing traffic on the dash (both done in
    step G)
  - the Navigation SDK's licence and attribution page (licensing)
  - auto-start (D8)

## Step G: the on-device map (ADR-0009)

The dash on the `crowpanel21-phone` build unless noted, the phone with the debug app. The
dash's `perf` line (`docs/building.md`) gives frame and map draw times; logcat tag `Map` gives
the phone's chunk sync. Use **Replay ×2** (108 km/h) on a recorded ride, or **Simulate ×20**.

| # | Check | How | Pass |
|---|---|---|---|
| G1 | Renderer bench | `crowpanel21-mapbench` build, five phases | ≥ 15 fps in every phase; no flicker, line shift or freezes on video |
| G2 | Map chunks on the phone | Maps settings → map desk check, online and with the offline region | Chunks made from OpenFreeMap and from the region; the tile cache fills |
| G3 | Chunk sync | Replay ×2 with the dash's cache empty; then toggle the dash's power mid-ride | About 80 KB in the first 10 s, about 100 B/s after; nothing refused; after a reset the phone starts over, after a reconnect it resends nothing held |
| G4 | Map on Nav | Replay ×2, then stop | The map follows the rider heading-up, smoothly; zooms out with speed and in before turns; "Map loading" only when slow, then "(c) OpenStreetMap" for 10 s |
| G5 | Traffic on the route | A Mappls route with traffic; Replay ×2 | Slow dashed, jam wider with ticks, unknown thin and dim, free solid; the route stands out from the roads in every theme; the ETA follows traffic |
| G6 | Styles per theme | Sim `--mapshots`; on the dash, each theme on Nav and Music | Each theme's own map style; the ring gauge looks as before (track, progress, round ends) and sits over the Nav card |

### Results, 2026-11-09 to 2026-11-14 (Nothing Phone (1), Android 15)

- **G1:** passes; numbers in ADR-0009 "Renderer bench". Two freezes the owner filmed were the
  bench's own log line and an unsmoothed heading, both fixed.
- **G2:** passes on the phone, online and offline (an 8.5 MB Goa region).
- **G3:** 67 KB in the first 10 s, then about 85 B/s at 108 km/h, nothing refused (ADR-0009
  "Chunk sync"). A reconnect resent nothing; a reset dash made the phone start over (fixed
  `dcf6923`).
- **G4:** the owner saw the map proper and smooth. Internal heap 44 KB free with BLE connected.
  Found and fixed: the built-in fonts have no ©, so the attribution reads "(c) OpenStreetMap".
- **G5:** passes after the owner's feedback that free-flowing route lines were hard to see in
  Classic and HUD: the route now has a casing and the roads are dimmer.
- **G6:** the owner approved the simulator screenshots of all three themes, with and without
  traffic. The ring gauge drawn by the rasteriser takes the stress-build Nav frame from 91 ms
  to 56 ms; its look on the dash is still to be confirmed by the owner.
- **Waits for a real ride:** traffic from Google guidance (F9).
