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
  to 56 ms; the owner confirmed its look on the dash (Nav and Music, every theme).
- **Waits for a real ride:** traffic from Google guidance (F9).

## Step H: real rides

Rides with the dash on the `crowpanel21-phone` build and the phone with the debug app. These
checks decide whether the phone build matches Phase 3 (the parity table below), after which the
Chronos build is removed.

**Rules for every ride:**

- **The phone stays locked in a pocket, screen off,** from **Start the ride** to the end. Search,
  shared links, backend choice and the confirmation screen all happen before the ride.
- **The dash is glance-only while moving.** Knob actions (Accept, a music control, Settings)
  happen stopped, or come from outside (someone calls you).
- **No laptop on the bike,** so the dash's serial log isn't there. The evidence is what you
  saw, the phone's logcat, and two Settings rows the dash keeps since power-on:
  - "Chip 61 C, max 74" ("Hot …" while the backlight is capped for heat)
  - "Drops 1, stale 3": phone link drops, and the times the route data went stale

  Read them at the end of each ride, before switching the dash off.
- **Ride logs never go in the repo.** They hold places and times. Results here quote counts and
  times only.

**Before each ride:**

1. Raise the phone's log buffer so a whole ride fits: Developer options → Logger buffer
   sizes → 16M (or `adb logcat -G 16M`; check with `adb logcat -g`). It lasts until the phone
   restarts.
2. Note the usage meters in Maps settings, and the backend.
3. For offline checks, the offline region is installed (Maps settings).
4. On the confirmation screen, note the start time and the ETA.

**After each ride:**

1. Write down the arrival time and the dash's two health rows.
2. Pull the log over wireless debugging:

   ```bash
   adb logcat -d -v time > ride-YYYYMMDD-N.log   # keep it outside the repo
   ```

   Useful tags: `Ride`, `Map`, `GoogleNav`, `RideLink`, `pose`. `Start proc
   io.github.d4rthc0d3r.sarathi` lines mean Android restarted the app mid-ride.

| # | Check | How | Pass |
|---|---|---|---|
| H1 | Each backend | One ride each with Google, Mappls, OSM (Valhalla), GraphHopper and OSM offline ("Route with"), each over 5 km with at least five turns | Turn by turn to arrival; the next turn and its distance right at every turn; the map keeps up; a missed turn reroutes on the same backend |
| H2 | Search and shared links | Before rides: pick the destination once by search on each backend, and once from a link shared from Google Maps and from Mappls | The ride ends where the place really is (not a nearby namesake) |
| H3 | Traffic colouring | A Mappls or Google route through a busy stretch at rush hour | The stretches you crawl in are slow or jam on the route line; free-flowing ones aren't. Traffic older than 5 min turns thin and dim (unknown), never left as it was |
| H4 | ETA accuracy | Every ride: the ETA at start against the arrival time | Arrival within 10 % of the first ETA (3 min on rides under 30 min), or the dash's ETA moved with the delays. Record each backend's error |
| H5 | Data drops | At a stop mid-route, mobile data off (quick settings), then ride on 10 min; data back on at the next stop | Guidance carries on; the map comes from the phone's cache or the offline region; traffic goes unknown after 5 min; a missed turn reroutes offline ("Now: OSM offline"); with data back, the next reroute uses the chosen backend again |
| H6 | Offline stretch | A whole ride inside the offline region with mobile data off from the start, routed "OSM offline" | Route, guidance, map and reroutes all work; no "Map loading" while inside the region |
| H7 | Call while navigating | Someone calls you while you ride; Accept on the dash only once stopped (or let it ring out) | The call screen takes over, then Nav returns; navigation was current the whole time (no "Navigation stale" when back) |
| H8 | Music while navigating | Music playing during a ride; at a stop, pause and skip on the dash | Title and time follow while riding; the controls work stopped; navigation unaffected |
| H9 | App killed | Desk, **Simulate ×20**, phone locked: (a) swipe the app from recents; (b) `adb shell am crash io.github.d4rthc0d3r.sarathi` (a process death); (c) `adb shell am force-stop io.github.d4rthc0d3r.sarathi`. On rides: look for `Start proc` lines | (a) the ride carries on. (b) the dash shows "No phone" or "No navigation" within seconds, never a frozen route; the link comes back by itself; the ride must be started again (not saved yet; decide after the rides). (c) "No phone" within seconds, never a frozen route |
| H10 | Tunnel or no GPS | Through a tunnel, a long underpass or a covered stretch | The map moves on for up to 5 s, then "Position stale Ns", never frozen silently; recovers within seconds of daylight, with no reroute from the jump |
| H11 | Heat | The dash in the sun an hour (riding, or parked with the screen on), then a ride | Readable in the sun; no flicker, line shift or reset. Note "Chip … max …". If "Hot" showed, the screen dimmed to at most 50 % and came back after cooling; the readings tune the 80 °C limit (`docs/hardware.md`, Heat) |
| H12 | Google within the free tier | After the rides: Maps settings' meters against Cloud Console (APIs & Services → each API's metrics) and Billing (this month's cost) | The meters match the console within a few calls; billing shows no charge; the quota caps are still set (ADR-0006) |

Checks carried over from earlier steps, done on the same rides:

- **D9:** the link stays up the whole pocket ride; the health row shows "Drops 0" (or each
  drop explained); `pose` fixes about once a second in logcat.
- **E7, E8:** Settings locks within about 2 s of riding off, and unlocks about 5 s after
  stopping (thresholds 5/3 km/h). With the lock switch off, they stay unlocked. A double-click
  at a stop shows it. Never try it while moving.
- **F9:** with Google guidance, `GoogleNav` "traffic:" lines in logcat and traffic on the dash.

### Parity with Phase 3

Each of `docs/phase3-test-plan.md`'s cases and what covers it on the phone build. "Recheck"
rows are a short desk session with the app before the owner confirms parity; the Chronos
build goes only after that.

| P3 | Case | On the phone build | Status |
|---|---|---|---|
| 1 | Pair a new phone | D2 (code on both, knob confirms) | Pass |
| 2 | Refuse pairing | C3 with the fake phone; with the app: Cancel on the dash | Pass |
| 3 | Paired phone reconnects | D6 (dash restart), D10 (Bluetooth off and on) | Pass |
| 4 | Forget phones | C13 with the fake phone; with the app: Forget phones, then the app pairs again | Pass |
| 5 | Navigation | F7, G4 on the desk (now with the map); H1 on rides | H1 |
| 6 | Navigation after a reconnect | G3 (resends only what's missing), D10; H9 | H9 |
| 7 | Settings lock | E7, E8 with GPS speed; without speed it still locks while navigating | E7/E8 |
| 8 | Incoming call | E3, E5 (with Accept and Reject instead of Dismiss) | Pass |
| 9 | Dismiss | E4 (Reject) | Pass |
| 10 | Music | E2 (now with time and progress) | Pass |
| 11 | Phone gone | C11, D7 | Pass |
| 12 | Update check with BLE | C12 with the fake phone; with the app: the link comes back by itself after the check | Pass |
| 13 | Power-up without a monitor | On the phone build: replug USB, no monitor | Pass |
| 14 | Knob on the music screen | On the phone build | Pass |
| 15 | Double-click | On every screen of the phone build | Pass |
| 16 | Clock | The phone sends the time on connect (the dash shows no clock; it sets day/night) | Pass |
| 17 | OTA with the BLE self-check | After the release switches to the phone build: a `v0.5.0-rc.N` pre-release installed over the air; the image confirms after BLE is up; the bonded phone reconnects without pairing | After parity |
| 18 | On the bike | H1-H12, D9, E7/E8, F9 | Rides |

### Results, 2026-11-14 (desk; Nothing Phone (1), Android 15)

- **H9 passes** on **Replay ×20** (Mappls, Calangute), phone locked, read from the dash's serial
  log and logcat:
  - **(a) Removed from Recents** (`am stack remove`, as a swipe): same process, the ride
    carried on.
  - **(b) Process death** (`am crash`): "No phone" at once; Android restarted the app within
    4 s, the link was Ready 3 s later and the dash showed "No navigation". The ride must be
    started again.
  - **(c) Force-stop:** "No phone" within 3 s. The companion device service then bound the app
    again by itself, and the link was Ready 12 s after the stop ("No navigation").
- **Parity rechecks:** P4 and P12–P16 pass with the app (the owner at the dash, the serial log
  and logcat). The update check paused BLE, which came back about 10 s later; the phone was
  Ready within a second, by itself. The phone sent the time on connect
  (`TimeSync(local=…, utcOffsetMin=330)`).
- **P2 passes:** after Forget phones, Cancel on the dash failed the phone's pairing
  (`SMP_NUMERIC_COMPAR_FAIL`, no bond) and the app showed "pairing refused"; it asked again 2 s
  later, and Pair bonded it and reached Ready. The phone's Bluetooth didn't stick this time.
- **Found and fixed:** the temperature sensor started with the RGB panel running left the
  picture shifted up about 20 rows on every boot (`7dc4fc1`). The firmware before step H, and the
  fixed order, boot straight, including from a cold replug.
- **Phone Bluetooth stuck, not ours:** after Forget phones, re-pairing and a dash replug, the
  phone stopped seeing the dash. It was advertising (a laptop saw it), the dash was on Android's
  accept list, and a fresh connect from the app waited. A Bluetooth toggle didn't help; a
  phone restart did. The phone's 256 KB log had rolled over, so raise it (`adb logcat -G 16M`)
  before desk tests too.

## Step I: the OSM contribution helper (ADR-0012)

The phone with the debug app; the dash on the `crowpanel21-phone` build. Logcat tag `Contribute`.

| # | Check | How | Pass |
|---|---|---|---|
| I1.1 | Off by default | Fresh install, or before ever turning it on: Maps settings | "Help fix OpenStreetMap: Off"; no `recording the GPS track` in logcat with the dash connected |
| I1.2 | Turning on explains | Turn the switch on | The dialog says what is recorded and what can be shared; Cancel leaves it off; Turn on sets it on |
| I1.3 | Recording | On, dash connected: `recording the GPS track`; dash off: `track recording stopped` | Starts and stops with the dash connection |
| I1.4 | Only while moving | Dash connected on the desk for a few minutes | No track file (`adb shell run-as io.github.d4rthc0d3r.sarathi ls files/osm-tracks`): standing still records nothing |
| I1.5 | On a ride | A ride with it on | One file per dash connection; Maps settings shows the rides and km |
| I1.6 | Turning off deletes | Turn it off, confirm | `deleted N track(s)`; the folder is empty; "Off" |
| I2.1 | Never during a ride | Start a ride (or Simulate), open Maps settings → Missing roads | "Available after the ride."; no check button |
| I2.2 | Check after a ride | Missing roads → Check my rides | Logcat: `<track>: N fixes, M tiles, K candidate(s)`; each candidate confirmed with Overpass; the places on small maps, north up, with OSM attribution |
| I2.3 | Overpass busy | (happens on its own) | `overpass busy: trying again in 15 s`; if still busy, "could not be checked yet" and tried again next time, never shown unchecked |
| I2.4 | Skip and dismiss | Skip one place, dismiss another, check again after the next ride | Skipped is hidden; dismissed is never offered again (`files/osm-gaps/dismissed.txt`) |
| I2.5 | A real gap | A ride through a road OSM doesn't have | It shows up; roads OSM has (tracks and paths too) and car parks don't |
| I3.1 | Sign in | Missing roads → Sign in to OpenStreetMap; log in and authorize in the browser | Back in the app, "Signed in"; logcat `osm login: signed in`; the token only encrypted in `shared_prefs/osm_account.xml` |
| I3.2 | Trace upload | Upload GPS trace on a place: check the map, the visibility and the description, then Upload | "Sent. Thank you for mapping."; the trace on the server, without the parts near the start, the end and saved places |
| I3.3 | Note | Add a note: edit the draft, Send | "Sent."; the note on the server at the place, with your words, signed by your account |
| I3.4 | Limits | A sixth trace or note in a day | "Not sent: today's limit of five is reached." |
| I3.5 | Sign out | Sign out | The buttons go; OSM's Authorizations page no longer lists the app |
| I4.1 | Refused | Add the road on a place whose end isn't near a road on the server | "This cannot be added from here: both ends must meet a road…"; nothing sent |
| I4.2 | Review | Add the road on a place with roads at both ends | The map: OSM's roads now, the new road, dots where it joins; road type and comment; Add is off until a type is picked |
| I4.3 | Added | Pick a type, Add the road | "Sent."; on the server one changeset (comment, `created_by`, `source=survey;gps`, `review_requested=yes`) with the new way (`highway`, `fixme`) joined at both ends; a road joined mid-segment keeps its tags |
| I4.4 | Conflict | Edit a joined road on the server between review and Add | "Not sent: OpenStreetMap changed there meanwhile"; the changeset closed with nothing in it |

### Results, 2026-11-14 (Nothing Phone (1), Android 15)

- **I1.1–I1.3 pass:**
  - Off on install; the dash connected logged no recording.
  - The dialog explains; Cancel leaves it off; the owner turned it on.
  - `recording the GPS track` at once.
  - The app's Stop logged `track recording stopped`; Start brought `recording the GPS track` again
    when the link was Ready.
- **I1.4 passes on the desk:** with the dash connected and the phone still, no track folder was
  created.
- **I1.5 waits for a ride. I1.6 (off deletes) is to do once a ride has recorded something.**
- **I2.2–I2.4 pass on the desk** with a test track placed over adb: 1 km from Miramar beach
  (Panaji) west over the sea, deleted afterwards.
  - 111 fixes, 12 tiles, 1 candidate.
  - Overpass answered 502 ("at capacity") at first. The client now backs off on 429 and 502–504
    (15 s, then 30 s); the place stayed unchecked until it answered.
  - Confirmed: "About 1010 m" on a small map with the coast roads.
  - Found and fixed: the map wasn't clipped and drew over the card.
  - Dismiss for good stored the place (a 561 m circle).
- **I2.1 and I2.5** wait for a ride.
- **I3 on the test server** (`master.apis.dev.openstreetmap.org`), with two test tracks over the sea
  off Miramar placed over adb:
  - **Found and fixed (setup):** the first login failed with HTTP 400 right after login. The OAuth
    app had been registered on the live osm.org, so the test server didn't know its client ID.
    It's now registered on the test server; the live registration is kept for later
    (`~/sarathi-secrets/osm/client-id`).
  - **I3.1–I3.3 pass:** signed in through the browser and back through `sarathi://osm-oauth`.
    The owner uploaded the trace of one place and added a note at the other, and saw "Sent.
    Thank you for mapping." for each.
  - I3.4 and I3.5 are still to do.
- **I4 not tried on a server yet (owner's choice, 2026-11-14).** The test server has no roads
  near the test places, so only the refusal could be shown there. The owner chose not to draw
  test roads, and to wait for a real missing road found on a ride. Before the guided edit is
  used on the live server, I4.2–I4.4 must pass on the test server.
