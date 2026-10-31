# ADR-0010: How the Android app keeps the ride link alive

- **Status:** Accepted (2026-10-31)
- **Date:** 2026-10-31
- **Related:** ADR-0001 (BLE only, phone is the brain), ADR-0007 (protocol v1), SECURITY.md

## Context

Hard rule: the phone stays locked in the rider's pocket with the screen off for the whole
ride. The link to the dash, and later location, guidance and rerouting, must keep running
headless through Doze and through the phone maker's battery savers. Android's rules (checked
2026-10-31, developer.android.com, foreground service types and background-start restrictions):

- A **foreground service** keeps the process running with the screen off. Type
  `connectedDevice` needs `FOREGROUND_SERVICE_CONNECTED_DEVICE` and a granted Bluetooth
  runtime permission. Type `location` needs `FOREGROUND_SERVICE_LOCATION` and a location
  permission.
- From the **background**, apps targeting Android 12+ may start a foreground service only with
  an exemption. A **CompanionDeviceManager** association with
  `REQUEST_COMPANION_START_FOREGROUND_SERVICES_FROM_BACKGROUND` is one.
- But a `location` service started from the background gets location only with
  **`ACCESS_BACKGROUND_LOCATION`**; the companion exemption doesn't lift that (Android 14+
  throws a SecurityException).

## Decision

1. **Pair through CompanionDeviceManager**, filtered on the dash's service UUID. The system's
   chooser does the scan, so the app has no `BLUETOOTH_SCAN` and uses no location to find the
   dash; Android stores the association. The app then bonds (numeric comparison, confirmed on
   the dash with the knob).
2. **The ride link is a foreground service** (`RideLinkService`) of type `connectedDevice`, plus
   `location` when Android allows it at that start. If the system refuses location to a start,
   it falls back to `connectedDevice` only and the app says so. It restarts if killed
   (`START_STICKY`).
3. **Ask for location "all the time"** (owner's decision), so location works when the link
   starts or restarts from the background, with the phone locked.
4. **Reconnect with `autoConnect`.** The Bluetooth chip holds a pending connection and connects
   when the dash advertises, so nothing in the app polls or sleeps; Doze doesn't stall it.
   Only failures back off (1 s to 30 s).
5. **Battery:** ask to be exempt from battery optimisation, and give per-maker instructions for
   the extra killers some makers add (dontkillmyapp.com).
6. **Auto-start (optional):** with the switch on, companion-device presence wakes the app when
   the dash appears nearby (Android 12+).

## Results on the test phone (Nothing Phone (1), Android 15, 2026-10-31)

- Foreground service running with types `connectedDevice|location` (`0x18`); every permission
  granted; the app on the battery-optimisation exemption list.
- **Deep Doze** (forced with `adb`): the link stayed up for minutes, a battery change reached
  the dash in the same second, and location fixes kept arriving.
- **Dash reset while the phone was locked in Doze:** reconnected 3 s after the dash booted,
  ready in under a second, and resent battery and time.
- **Auto-start does not work on this phone yet:** presence did wake the app when the switch
  was turned on with the dash nearby, but after the dash was unplugged for 10 minutes Android
  still listed it as nearby. With no "disappeared", there is no new "appeared". **Planned fix
  (deferred):** with auto-start on, keep the ride link waiting all the time (`autoConnect`,
  connected-device type only; location only while connected), started at boot, with presence
  kept as a fallback.

## Consequences

- One ongoing notification while the link runs; it has Stop.
- The phone must grant five permissions and the battery exemption; the app asks one at a time,
  each with what the rider gets.
- **A dash that changes firmware** (the Chronos build, or an update that changes services)
  leaves Android with a stale cached service list for a bonded phone. The dash sends Service
  Changed once per phone per boot, and the app re-reads the services if that arrives during
  setup.
- Indoors, the location check only sees Wi-Fi and cell fixes (about one every 4 s); a real
  pocket ride must confirm GPS at 1 Hz.
