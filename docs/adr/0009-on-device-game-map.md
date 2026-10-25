# ADR-0009: On-device game-style map (minimap)

- **Status:** Accepted (2026-10-25)
- **Date:** 2026-10-25
- **Extends:** ADR-0002 (vector route window). **Related:** ADR-0001, ADR-0003 (themes),
  ADR-0006 (backends, traffic), ADR-0007 (protocol v1)

## Context

ADR-0002 put a heading-up route line on the device, drawn from a 64-point window around the
rider. The owner wants a game-style minimap (GTA/NFS): the roads around the rider, the
route highlighted and coloured by traffic, all rendered on the device. Hard rules:

- **BLE only;** no images or Wi-Fi streaming to the device. The device draws the map itself
  from vector data.
- **The phone stays locked in a pocket,** so map data must flow without the phone's screen,
  and stretches with no signal must still have a map.
- **Free sources only;** never `tile.openstreetmap.org`.
- **The provenance of every piece of map data is known.** Google-derived data never mixes
  into OSM data and is never uploaded to OSM.

### Measured: how big is the map? (2026-10-25)

- **Input:** real OpenFreeMap tiles (OpenMapTiles schema, planet build 2026-10-18), one z14
  tile each (about 2.3 km wide, 5.4 km²).
- **Processing:** roads simplified (Douglas–Peucker) and quantized to device resolution.
- **Encoding:** binary is a class byte, a point count and zigzag-varint coordinate deltas
  (ADR-0007).
- Script: `tools/measure_chunks.py`, to be added with the protocol code.

| Area | City detail L0 (all roads to `service`, 2 m) | Highway detail L1 (`motorway`–`secondary`, 8 m) |
|---|---|---|
| Pune centre | 1,187 lines, 3,758 points: **12.3 KB binary** (JSON 54.8 KB) | 1.1 KB |
| Bengaluru centre | 1,449 lines, 4,368 points: **14.5 KB** (JSON 64.4 KB) | 2.2 KB |
| Mumbai–Pune expressway, ghat | 2.3 KB (JSON 10.7 KB) | 0.3 KB |
| Rural Karnataka | 0.07 KB | 0 KB |

- **Binary is about 4.5× smaller than JSON;** deflate on top saves only another 10–25 %.
- **A dense city centre is at most about 2.6 KB/km²** at full detail. Highways are tens to
  hundreds of bytes per km².

## Decision

### Data on the phone

- **Sources:**
  - **Online:** OpenFreeMap vector tiles (no key, no request limits, commercial use allowed,
    attribution "© OpenMapTiles, data from OpenStreetMap").
  - **Offline:** a region file the owner downloads once, a **Protomaps extract**
    (`pmtiles extract` of a daily planet build; BSD and ODbL). OpenFreeMap caps in-app
    offline downloads at 3,000 tiles or 50 MB each, so a state-sized region comes from the
    PMTiles file instead.
- **Layers:** roads by class (`motorway`, `trunk`, `primary`, `secondary`, `tertiary`,
  `minor`, `service`), optionally water polygons and parks. No buildings, no labels except
  the next street name, which comes from guidance.
- **Chunks:** one chunk per tile per level of detail:

  | Level | Tile | Classes | Quantum | Used at |
  |---|---|---|---|---|
  | L0 | z14 (about 2.3 km) | all road classes to `service` (+ water, parks if enabled) | 2 m | ≤ 4 m/px (city, near turns) |
  | L1 | z12 (about 9 km) | `motorway`–`secondary` (+ large water) | 8 m | > 4 m/px (highway) |

- **Processing:**
  - Simplify to the level's tolerance and quantize to integers relative to the chunk's
    origin in the **ride frame** (below).
  - Merge connected segments of the same class into longer polylines (fewer headers).
  - Clip to the chunk.
- **Provenance:** every chunk carries a provenance code (`OSM_OPENFREEMAP`,
  `OSM_PROTOMAPS_OFFLINE`; more later) and a data-version tag. Only OSM-derived data is ever
  stored as chunks. **Google or Mappls route geometry never becomes chunk data:** it's only
  ever a route message, tagged with its backend.
- **Phone cache:** processed chunks on the phone's storage, keyed by (level, tile x, tile y,
  data version), capped (default 200 MB, LRU). Ahead of a ride the app prefetches the route
  corridor, so a stretch with no signal still has its map.

### Ride frame (what the device knows about position)

- **A local metric frame per ride:**
  - Origin near the start, rounded to 1 km.
  - x east and y north in metres (local equirectangular projection; error under 0.1 % within
    150 km).
  - Integer coordinates.
- **The device never receives latitude or longitude.** That keeps ADR-0002's property: the
  device can't tell where on Earth the ride is.
- **Re-anchoring:** if the rider gets more than 150 km from the origin, the phone starts a
  new frame epoch. The device drops its chunks and route, and the phone resends what's near.
- **Rider pose:** x, y, heading, speed and a timestamp, sent at **2 Hz**. The device
  dead-reckons between updates (heading-rate and speed) to draw at up to 15 fps.

### Device: chunk cache

- **PSRAM, allocated once at init:** 1.5 MB for chunks (about 110 dense city L0 chunks,
  about 600 km², or far more highway area). Plus a fixed route buffer of up to 4,000 points
  and up to 256 traffic stretches.
- **Eviction:** least recently drawn first, but never chunks within 1 km of the rider or of
  the next 2 km of route.
- **After a reconnect:** the device reports which chunk keys it holds (ADR-0007); the phone
  sends only what's missing.

### Device: drawing

- **Layout (heading-up):** the rider arrow in the lower third, as in Phase 1.
- **Zoom by speed:**
  - 2 m/px at 0–20 km/h, rising linearly to 10 m/px at 100 km/h.
  - **Zoom in before turns:** within 300 m of a maneuver, ease to 2 m/px (extends
    `core::NavMetresPerPx`).
  - Hysteresis so it doesn't pump.
- **Draw order:**
  1. background
  2. water and parks (optional per style)
  3. roads by class, minor first
  4. the **route** as a highlighted line, wider than any road and coloured by traffic
  5. rider arrow
  6. overlays: maneuver arrow, distance, traffic-aware ETA, and **only the next street
     name** as text
- **The route always draws,** even where OSM has no road. It's its own geometry, never
  snapped to chunk roads.
- **Minimal style:** the route line on a plain background. It's both a selectable style and
  the automatic fallback while chunks are missing (shown as "Map loading", never silently).
- **Stale data:**
  - **Pose:** no update for over 3 s shows "Position stale" and stops dead-reckoning after
    5 s.
  - **Traffic:** older than 5 minutes draws as `UNKNOWN`.

### Per-theme map styles (ADR-0003)

- **Map style data in the theme descriptor** (`sarathi_core`): background, road width and
  colour per class, water and park fills, route width and colour, rider arrow, and a traffic
  style per level.
- **Three styles:**

  | Theme | Map style |
  |---|---|
  | Classic | Filled roads on dark ground, route in the theme accent |
  | Fighter-jet HUD | Wireframe: thin single-stroke roads in HUD green, no fills; route as a brighter, thicker stroke |
  | Racing | Neon: roads as thin bright lines with a wider dim "glow" stroke under each (two passes, so budget it); route in hot pink or cyan |

- **Traffic is never colour alone.** Each level has its own **pattern and width**:

  | Level | Pattern | Width | Example colour (per theme) |
  |---|---|---|---|
  | FREE | solid | route width | theme route colour |
  | SLOW | long dashes | route width | amber or yellow |
  | JAM | solid with cross-ticks every 20 px | route width + 4 px | red or magenta |
  | UNKNOWN | solid, thinner | route width − 2 px | theme route colour, dimmed |

- **Readable in sunlight and for colour-blind riders:** new native checks in
  `test_theme_legibility`:
  - contrast of each traffic colour against the map background (≥ 4.5:1) and against
    adjacent road colours
  - all levels stay distinguishable under simulated protanopia, deuteranopia and tritanopia
    (Machado et al. matrices)
  - each level's pattern is unique

### Performance plan (target ≥ 15 fps while rotating, 480×480)

- **Phase 1 measurements:**
  - A full-screen redraw costs about 100 ms: 45–60 ms rendering plus **42 ms flush** (R/B
    swap and copy into the PSRAM framebuffer).
  - So 15 fps (66 ms) needs both halves cut.
- **Flush:**
  1. Drop the R/B swap pass by building the palette pre-swapped (theme binding converts
     colours once).
  2. Render **directly into the panel framebuffer** (LVGL direct mode with two framebuffers
     swapped on vsync), removing the copy. GFX Library's RGB panel may not support two
     framebuffers; ESP-IDF's `esp_lcd` RGB driver does (`num_fbs = 2`). That's a driver
     change in `sarathi_board`, so a spike decides.
- **Rendering:**
  - **Culling:** chunk bounding boxes against the rotated view, then per-polyline boxes.
  - **Level of detail by zoom** (L1 only above 4 m/px).
  - **Skip** `service` roads above 3 m/px and segments shorter than 1 px after projection.
  - **Fixed-point** rotation (Q16) on the UI task, projecting once per frame into a scratch
    buffer.
- **Redraw thresholds:** redraw the map only when the heading changes by ≥ 1.5°, the
  position moves ≥ 1 px at the current zoom, the zoom changes, or new data arrives. Cap at
  15 fps. A still bike on a static screen costs 0 fps.
- **Panel flicker risk:** sustained full-screen redraws load PSRAM, the same bus the panel
  scans out of, which risks line shift.
  - Keep the 480×20 bounce buffer.
  - Measure 30 minutes at 15 fps, watching for shift.
  - If it shows, cap at 10 fps or redraw only the map area.
  - Flash writes stay forbidden during a ride (no NVS saves while navigating).
- **Spike, before the protocol is frozen:** render Pune's L0 chunk rotating at 15 fps on the
  device. Report frame times, flush times and stability. **Proceed only if ≥ 15 fps, or if
  the owner accepts 10.**

### BLE budget

- **Assumed usable throughput:** 8 KB/s, conservative.
  - MTU 247, phone in a pocket, Android's background connection interval of about 30–50 ms.
  - ESP32-S3 with NimBLE manages far more in the foreground; **measure** in the spike.
- **Steady state:**

  | Traffic | City (40 km/h) | Highway (100 km/h) |
  |---|---|---|
  | Map chunks along the route | one L0 chunk (13 KB) every ~3.5 min ≈ **60 B/s** | one L1 chunk (5–35 KB) every ~5.5 min ≈ **100 B/s**, plus L0 near each upcoming maneuver |
  | Rider pose, 2 Hz × 16 B | 32 B/s | 32 B/s |
  | Guidance state, 1 Hz | ~60 B/s | ~60 B/s |
  | Traffic, on change, ≤ 1 per 30 s, ~50 stretches × 6 B | ~10 B/s | ~10 B/s |
  | **Total** | **about 0.2 KB/s, 2–3 % of budget** | **about 0.2 KB/s** |

- **Bursts:**
  - **Ride start:** route geometry (50 km at 5 m tolerance: about 800 points, about 2.5 KB)
    plus prefetching the first 10 km of corridor (about 6 city chunks, 80 KB): **about 10 s**.
  - **Reroute:** a new route, about 2.5 KB, under 1 s.
  - **Reconnect:** missing chunks only.
- **Priorities:** guidance and pose first, then route and traffic, then chunks. A chunk
  transfer never delays the next maneuver.

## Consequences

- **A real minimap,** within a few percent of the BLE budget. The map stays useful through
  signal gaps (phone cache) and through BLE drops (device cache).
- **New pieces:**
  - Phone: tile fetching, MVT decoding, simplification and a chunk cache.
  - Device: a chunk store, a renderer and per-theme map styles in `sarathi_core` with
    native tests.
  - Possibly an `esp_lcd` direct-mode display driver.
- **Rendering speed is the main risk;** the spike decides before the protocol is frozen.
- **Attribution:** "© OpenStreetMap" is shown on the map screen when the map starts and in
  About. This meets ODbL's "reasonably calculated" requirement without permanent clutter at
  ride time.

## Sources

- OpenFreeMap: [openfreemap.org](https://openfreemap.org/) (terms, offline caps, attribution)
- Protomaps: [getting started (extracts)](https://docs.protomaps.com/guide/getting-started),
  [basemap layers](https://docs.protomaps.com/basemaps/layers)
- Measurements: OpenFreeMap TileJSON `https://tiles.openfreemap.org/planet` (build
  20261018_080001_pt), z14 tiles 11553/7334, 11723/7596, 11531/7322, 11694/7560
