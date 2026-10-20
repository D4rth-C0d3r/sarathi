# ADR-0002: Navigation map as a vector route window drawn on the device

- **Status:** Accepted
- **Date:** 2026-10-20
- **Related:** ADR-0001 (phone is the brain), ADR-0003 (themes)

## Context

Navigation must show a **map of what's coming**, not only a turn arrow and a distance. The
rider should see the shape of the road ahead (bends, the next junction, where the turn is)
in a one-second glance.

Constraints:

- **Device.** ESP32-S3, 480×480 round RGB panel, LVGL 9.1, 8 MB PSRAM, 6 MB OTA slots. No
  GPS, no storage for map data (ADR-0001).
- **Link.** BLE only to the phone, MTU ≥ 247. Practical BLE throughput is on the order of
  tens of KB/s at best, and it shares the link with calls, media and control messages.
  Wi-Fi is reserved for OTA.
- **Heading-up.** A dash map must rotate with the bike, smoothly and with low latency;
  north-up maps are hard to read while riding.
- **Themes.** Screens must follow the selected visual theme (ADR-0003), including the map.
- **Data source.** Google Maps' navigation notification, which Phase 3 (Chronos) and a
  notification-listener approach would read, carries the next maneuver as text, an icon and
  a distance. **It has no route geometry.** A map needs a routing source that exposes the
  route polyline.

## Decision

**The phone sends a vector route window; the device draws it heading-up with LVGL.**

- The phone owns routing and positioning. It keeps the full route and, on each update,
  sends a **route window**: the part of the route around the rider (for example the next
  ~1–2 km and a short stretch behind), simplified to what is visible at screen scale.
- Geometry is a **polyline in local metres relative to the rider**: the rider is at the
  origin, axes fixed to east/north. No latitude/longitude goes to the device.
- Each window also carries the rider's **heading**, the **next maneuver** (type, position on
  the polyline) and the **distance to it**. Small pose updates (heading, distance travelled)
  can be sent between windows so the device stays smooth without resending geometry.
- The device rotates and scales the points itself (heading-up, rider chevron fixed near the
  lower centre), clips to the round screen and draws with LVGL vector primitives. Colours
  and line widths come from the active theme.
- The next maneuver and its distance stay the **primary** item on the nav screen; the map is
  supporting context under it (root rule: one primary item per screen).
- If geometry is missing or stale, the nav screen **falls back to arrow-only**. Phase 3
  (ChronosESP32) is arrows-only by design.
- The exact message format (field names, units and fixed-point scaling, limits on point
  count, update rates) is defined in `docs/protocol/` before any code, per the protocol
  rule. CLAUDE.md lists it as a new message type, `route`.

## Alternatives considered

### A. Raster snapshots over BLE

The phone renders a map image and sends it to the device.

- **Bandwidth.** A raw 480×480 RGB565 frame is 450 KB; even a well-compressed JPEG or PNG is
  tens of KB. Over BLE that is roughly a second or more per frame, competing with other
  traffic.
- **No smooth heading-up.** Every heading change needs a new frame from the phone, so the
  map would rotate in visible jumps, seconds behind the bike.
- **Device cost.** Decoding a full-screen JPEG on the ESP32-S3 takes noticeable time per
  frame and a large PSRAM buffer, on top of the LVGL buffers.
- **Not themeable.** Colours and styles are baked into the image by the phone's map
  renderer (conflicts with ADR-0003).
- **Licensing.** Rendering map tiles for an external display brings tile-provider terms into
  play (for example Google's and OSM's tile usage policies), which a polyline we draw
  ourselves avoids.

### B. Raster snapshots over Wi-Fi

Fixes bandwidth, but needs a phone ↔ device Wi-Fi link (hotspot or Wi-Fi Direct), which
ADR-0001 rejected for power and pairing complexity. Heading-up would still be choppy, and
it is still not themeable or licence-free.

### C. Map data and rendering on the device

Needs map storage, a renderer and positioning on the device. Doesn't fit the flash budget
or ADR-0001.

### Why vector wins

| | Vector route window | Raster over BLE | Raster over Wi-Fi |
|---|---|---|---|
| Data per update | ~1–2 KB | tens of KB | tens of KB |
| Heading-up rotation | local, at display rate | per frame from phone | per frame from phone |
| Follows the theme | yes | no | no |
| Device flash/PSRAM | tiny | decode buffers | decode buffers |
| Extra radio link | no | no | yes |
| Privacy | relative metres only | rendered map | rendered map |

## Consequences

**Good**

- Small messages fit BLE comfortably and leave room for calls and media.
- Smooth heading-up rotation, crisp lines at any zoom, and themed colours.
- No image assets for the map; flash cost is code only.
- The device never learns the rider's absolute position.

**Costs and follow-ups**

- **Phase 4 needs its own routing source that exposes geometry.** Google Maps'
  notification can't feed the map. Candidates to compare in a later ADR: a hosted routing
  API, an OSM-based router (OSRM, GraphHopper, Valhalla; hosted or on the phone), or GPX
  import. Terms of use matter: some routing APIs only allow results to be shown on their
  own map, and OSM data needs ODbL attribution.
- **Phase 3 stays arrows-only.** Chronos and notification data have no geometry.
- **Phase 1** builds the nav screen and map renderer against **simulated** route windows,
  so rendering is proven before any phone code exists.
- The device must handle stale or missing data visibly (fallback to arrows, "GPS lost"),
  and the phone must keep windows small (simplification, point limits).
- Rendering cost on the device (point transform, clipping, LVGL redraw of the map area at a
  useful frame rate without panel flicker) is unproven. A Phase 1 spike must measure it
  before the format is fixed.

## Implementation status

Phase 1 implements the device side against the demo ride: `core::route_projection`
(heading-up rotation, scale, clipping to the circle, splitting at the rider; native tests)
and the `RouteView` component on the Nav screen. The window is redrawn when a new one
arrives (about once a second), so rotation currently updates at that rate. Rendering cost on
the device is measured with `-DSARATHI_PERF_OVERLAY` (see `docs/building.md`) before deciding
on smoother rotation and before the `route` message format is fixed.
