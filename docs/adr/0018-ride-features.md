# ADR-0018: Ride features: trip log and speed limits

- **Status:** Accepted (2026-11-14)
- **Date:** 2026-11-14
- **Related:**
  - ADR-0003 (themes and legibility)
  - ADR-0006 (backends)
  - ADR-0009 (route window and tracker)
  - ADR-0011 (moving detection)
  - ADR-0012 (OSM data use)
  - CLAUDE.md "Rider safety"

## Context

The owner asked for two features:

- **A trip log on the phone:** distance, time, average and maximum speed, the route, and GPX
  export, stored only on the phone.
- **The speed limit on the dash,** from OSM's `maxspeed` and from the backend where it has one,
  with an over-limit alert in every theme. **Unknown limits show nothing,** never a guess.

### Data sources

- **The map chunks carry no speed limits.** The OpenMapTiles and Protomaps schemas have no
  `maxspeed` in their road layers.
- **The phone already gets** a position about once a second (`PoseSender`, fused provider) and
  the route's geometry.
- **OSM `maxspeed`:**
  - **Coverage in India is sparse,** so "unknown" will be common.
  - **Values:** numbers (`50`), units (`30 mph`), `none`, and implicit codes (`IN:urban`).

## Decision

### 1. Trip log (phone only)

- **What a trip is:** it starts when the dash is connected and the bike starts moving (the moving
  detector's rule: about 5 km/h for 2 s, ADR-0011). It ends after **10 minutes** without moving,
  or 10 minutes after the link ends.
  - **Merging:** a stop for fuel stays in the trip; a night's stop doesn't.
- **What it records:** the phone's own positions, the ones `PoseSender` already receives. It
  never records the backend's planned route: Google's route geometry isn't stored (ADR-0006).
- **Filtering:**
  - Fixes worse than 25 m accuracy are dropped.
  - Spikes are dropped: an implied speed over 70 m/s, or a jump against the heading.
- **Statistics:**
  - **distance:** the sum between kept fixes while moving
  - **moving time:** time at 3 km/h or more
  - **total time:** start to end
  - **average speed:** distance over moving time
  - **maximum speed:** the highest speed held for at least 3 s, so one bad fix can't set it
- **The track:** simplified to 5 m (Douglas-Peucker, as in ADR-0012), for the trip's map and GPX.
- **Storage:** app-private, one file per trip, never backed up (backup is off for the app).
  - **Kept until deleted.** A setting can expire trips: 30, 90 or 365 days, or never.
  - **On by default, with a switch,** since nothing leaves the phone (the owner, 2026-11-14).
- **GPX export:** through Android's share sheet only, one trip or several.
  - **Contents:** the simplified track with times.
  - **Optional privacy cut:** an option ("leave out the first and last 300 m") reuses ADR-0012's
    privacy cut.
- **Separate from the OSM helper's raw tracks (ADR-0012):**
  - **Different purpose and data:** those are satellite-only fixes, opt-in, kept 30 days for
    finding gaps.
  - **No sharing between them:** neither feeds the other.
- **App screens:** a list of trips, and one trip with a map ("© OpenStreetMap contributors"), its
  numbers, Export and Delete. Nothing new on the dash.

### 2. Speed limits

**Sources, in order of trust:**

1. **The backend's own limits,** where the backend gives them for the route.
   - **Mappls:** to check whether its route response has limits; used only if it does.
   - **Google's Navigation SDK:** has its own speed limit data, but only in the owner's "full"
     builds (ADR-0014), under the same private-use decision as Google's route (ADR-0006). It's
     never in public builds.
2. **OSM `maxspeed` along the route:**
   - **When:** fetched **before the ride** (on the confirmation screen, phone in hand) and after
     each reroute, through Overpass.
   - **The query:** ways tagged `highway` and `maxspeed` within 15 m of the route, the route
     thinned to a point every 100 m.
   - **One query per route** (version), within Overpass's limits (ADR-0012).
3. **Offline:** Valhalla's offline tiles carry OSM speed limits. If valhalla-mobile exposes them
   (`trace_attributes` or the route's edges, **to verify**), they're used when there's no network.

**Matching:**
- **Ways to the route:** each route segment takes the limit of the OSM way it runs along: within
  15 m and within 20° of its heading. Where ways disagree or none matches, the limit there is
  unknown.
- **Values:**
  - **Numbers only:** a number in km/h, or in mph converted.
  - **Unknown:** `none`, `signals`, `walk` and **implicit codes** like `IN:urban` all count as
    unknown. A country default is a guess, and guesses are never shown.

**To the dash:**
- **Message:** a binary `limits` message, like `traffic`: the route ID and version, then
  stretches `(offset_m, length_m, kmh)`.
- **Display:** the dash shows the limit for the rider's current offset along the route (the route
  tracker already has it).
- **Nothing is shown when:**
  - the stretch is unknown
  - the rider is off the route
  - the data is stale (older route version)
- **Protocol:** a minor version, spec first.

**On the dash:**
- **The sign:** a round speed-limit sign, the number at the supporting size (≥ 34 px). It sits in
  the Nav status area, so the next maneuver stays the one primary item (CLAUDE.md).
- **Over the limit:** the GPS speed (`pose`) must exceed the limit by **more than 3 km/h or 5 %,
  whichever is larger, for 3 s**. It clears after 3 s at or under the limit.
- **While over:**
  - the sign's ring thickens, with a pattern, not only a colour change
  - the current speed appears next to it at the essential size (≥ 46 px)
  - nothing flashes, nothing moves, and there's no sound (CLAUDE.md, ADR-0003)
- **Per theme:**
  - **Style:** each theme gives the sign's style (ring colour, fill, the alert ring's pattern) in
    its descriptor.
  - **Legibility:** `test_theme_legibility` checks contrast (≥ 7:1 for the alert speed, ≥ 4.5:1
    for the limit number) and that the alert differs by more than colour.
- **Settings (dash and app):**
  - "Speed limit: show / hide"
  - "Over-limit alert: on / off"

  Both default on. The alert never requires an action.

## Consequences

- **Phone:** a trip recorder alongside `PoseSender`; a speed-limit source in `core:maps` (pure,
  tested with recorded Overpass answers); new screens.
- **Firmware:**
  - **`sarathi_core`:** limit lookup by route offset and the over-limit hysteresis, as pure logic
    with native tests.
  - **`sarathi_ui`:** the sign component, a semantic component.
- **Protocol:** the `limits` message, spec first, with golden vectors.
- **Rides:** both features need real rides to check, like Phase 4's step H.
