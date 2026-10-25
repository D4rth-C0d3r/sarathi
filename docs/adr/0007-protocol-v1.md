# ADR-0007: Phone–device protocol v1

- **Status:** Accepted (2026-10-25)
- **Date:** 2026-10-25
- **Related:** ADR-0001 (BLE only), ADR-0002 and ADR-0009 (map), ADR-0005 (pairing and MTU
  lessons from Chronos), ADR-0006 (backends, traffic)

## Context

Phase 4 replaces Chronos with our own GATT service. What the protocol has to carry:

- **Control messages** (CLAUDE.md's list): `hello`, `nav`, `nav_end`, `call`, `call_action`,
  `media`, `media_cmd`, `time`, `phone_batt`. OTA messages are reserved for Phase 5.
- **New from ADR-0009:** rider pose, route geometry, per-segment traffic, and map chunks with
  a sync mechanism.
- **New from ADR-0006:** the active backend, its attribution and fallback events.

Lessons from Phase 3 apply:
- Require a bonded, MITM-authenticated link.
- The device starts the MTU exchange itself.
- Never let a reassembly bug corrupt memory.
- Writes arrive on NimBLE's host task, which only queues them.

### Measured sizes (2026-10-25, real data; see ADR-0009)

| Message | JSON | Compact binary | Transfer at 8 KB/s (binary) |
|---|---|---|---|
| Map chunk, Pune centre L0 (z14, 3,758 points) | 54.8 KB | **12.3 KB** | 1.5 s |
| Map chunk, Bengaluru centre L0 | 64.4 KB | **14.5 KB** | 1.8 s |
| Map chunk, highway L1 (z12) | 1.5–10 KB | **0.3–2.2 KB** | < 0.3 s |
| Route, 50 km, about 800 points at 1 m quantum | about 9.6 KB (`[[x,y],…]`) | **about 2.4 KB** | 0.3 s |
| Traffic, 50 stretches | about 1.5 KB | **about 250 B** | — |
| Rider pose | about 80 B | **18 B** | — |
| Guidance state (`nav`) | about 120 B | about 40 B | — |

Binary is 4–5× smaller for geometry. For small control messages the difference doesn't
matter at their rates, and JSON stays readable in logs and easy to keep in step between C++
and Kotlin.

## Decision

### GATT layout

- **One custom primary service** with two characteristics:

  | Characteristic | Direction | Properties |
  |---|---|---|
  | `rx` | phone → device | Write Without Response (bulk), Write (only for `hello`) |
  | `tx` | device → phone | Notify |

  UUIDs are generated once and fixed in `protocol.md`.
- **Security:** both characteristics require an **encrypted, bonded and authenticated** link.
  The pairing flow is the one from Phase 3: the code is confirmed with the knob, and Forget
  phones exists.
- **MTU:** the device starts the exchange on connect and asks for 517. The design assumes
  **247** (244-byte payload), CLAUDE.md's minimum. Larger MTUs just mean fewer fragments.
- **Phase 5** adds an OTA characteristic; v1 reserves the message types only.

### Framing (every write and every notification is one frame)

```
first fragment:  type:u8  msg_id:u8  frag:u8  total_len:u16  crc16:u16  payload...
other fragments: type:u8  msg_id:u8  frag:u8  payload...
frag = bit7 FIRST | bit6 LAST | bits0-5 fragment index (0..63)
```

- **Message size:** up to 64 × 243 bytes, about 15 KB, which covers the largest measured
  chunk. Bigger chunks are split by the phone, by area.
- **`msg_id`** rolls per direction. A new FIRST with a different `msg_id` abandons an
  unfinished message (counted, like Phase 3's receiver).
- **CRC-16/CCITT over the whole message.** BLE's link-layer CRC already covers each packet;
  this catches reassembly bugs and dropped writes.
- **Receiver rules:** every length, index and count is checked, as in
  `sarathi_link/chronos_packets`. A failure drops the message and is logged. Never trust a
  length.
- **Flow control:** the device grants **credits**, the number of frames it can buffer. A
  `flow` message at connect and then periodically sets the window. The phone never sends
  more frames than its credits, so the RX queue can't overflow.

### Encoding: JSON for control, binary for geometry

- **JSON** (UTF-8, one object, `"t"` = type, short keys) for:
  - existing types: `hello`, `nav`, `nav_end`, `call`, `call_action`, `media`, `media_cmd`,
    `time`, `phone_batt`
  - new types: `backend`, `event`, `frame`, `flow`, `chunk_ack`, `style`
- **Binary** (little-endian, zigzag varints for deltas, a version byte first) for: `pose`,
  `route`, `traffic`, `chunk`, `chunk_have`.
- **This changes CLAUDE.md's "JSON messages" rule** to "JSON for control messages; compact
  binary for pose, route, traffic and map chunks (ADR-0007)". Owner's decision.

### Messages v1

**Phone → device**

| Type | Enc | Rate | Content |
|---|---|---|---|
| `hello` | JSON | at connect | `proto` major.minor, `app` version, phone capabilities |
| `time` | JSON | at connect, then every 10 min | local time, UTC offset |
| `phone_batt` | JSON | on change | level, charging |
| `frame` | JSON | at ride start and on re-anchor | ride-frame `epoch` (u16). No lat/lon: the device never learns the absolute position. |
| `backend` | JSON | at ride start and on change | active routing and search backend ids, display name, attribution text, `traffic` available, `two_wheeler` used |
| `event` | JSON | when it happens | `kind`: `fallback` (from, to, reason: `quota`, `offline`, `error`, `no_route`), `reroute`, `cost_warning` (backend, percent) |
| `nav` | JSON | 1 Hz and on change | maneuver enum and roundabout exit, distance to it (m), next street name (≤ 48 B UTF-8), traffic-aware remaining time (s), remaining distance (m), flags: `off_route`, `rerouting`, `arrived` |
| `nav_end` | JSON | once | reason |
| `pose` | bin | 2 Hz | epoch u16, phone time ms u32, x/y i32 (decimetres in the ride frame), heading u16 (0.01°), speed u16 (0.01 m/s), accuracy u8 (m) |
| `route` | bin | at start and on reroute | `route_id` u32, version u16, backend u8, epoch u16, point count, points (1 m quantum, zigzag deltas), maneuver markers (point index, maneuver, exit) |
| `traffic` | bin | on change, at most every 30 s | `route_id`, version, count, entries (offset m varint, length m varint, level u8: free/slow/jam/unknown) |
| `chunk` | bin | as needed, lowest priority | key (level u8, tile x u32, tile y u32), data version u32, **provenance u8**, epoch, origin x/y i32 (m), quantum u8 (m), polylines (class u8, count varint, zigzag deltas), optional water and park polygons |
| `call` | JSON | on change | phase (`ringing`, `active`, `idle`), caller (≤ 32 B) |
| `media` | JSON | on change, plus position every 5 s while playing | available, playing, title, artist, position s, duration s |

**Device → phone**

| Type | Enc | Content |
|---|---|---|
| `hello` | JSON | `proto` major.minor, firmware version and seq, device capabilities (map styles, chunk cache size), current `route_id`/version and `epoch` it holds |
| `chunk_have` | bin | epoch, then the keys and data versions it holds (sent after `hello` and after `frame`) |
| `chunk_ack` | JSON | keys stored, keys refused (no space or bad data) |
| `flow` | JSON | credits (frames it can take) |
| `style` | JSON | active map style and which optional layers it draws (water, parks), so the phone doesn't send unused layers |
| `call_action` | JSON | `accept`, `reject` |
| `media_cmd` | JSON | `play_pause`, `next`, `prev` |

- **No navigation control from the device** (no destination entry, no "start route"). That
  keeps the rule against interacting while riding.

### Sync and resync

- **Connect:**
  1. Bond and encryption, then the MTU exchange.
  2. The two sides swap `hello`s; the device sends `flow`, `style` and `chunk_have`.
  3. The phone sends `time`, `phone_batt` and, if a ride is active, `frame`, `backend`, then
     `route` (unless the device already holds the same `route_id` and version), then
     `traffic`, then `nav` and `pose`.
  4. Missing chunks for the corridor follow, nearest first.
- **Chunks:** the phone tracks the device's inventory from `chunk_have` and `chunk_ack`, and
  only sends what's missing. After a reconnect the device re-sends `chunk_have`, so nothing
  is resent needlessly.
- **Re-anchor:** a new `frame` epoch tells the device to drop chunks and route from the old
  epoch.
- **Staleness on the device:**
  - No `pose` for 3 s shows "Position stale".
  - No `nav` for 10 s shows "Navigation stale".
  - Traffic older than 5 minutes draws as unknown.
  - A disconnect shows "Phone not connected".
  - Never silently frozen (CLAUDE.md).

### Scheduling on the phone

**Priority queue:** `nav`, `call`, `pose` > `route`, `traffic`, `event`, `backend` > `media`
> `chunk`. A chunk's fragments are interleaved with higher-priority frames between
fragments, so a 1.5 s chunk never holds back a maneuver update.

### Versioning

- **`proto` = major.minor, separate from the firmware version.**
  - **Major mismatch:** the device shows "Update the app" or "Update the firmware" and ignores
    data.
  - **Minor:** additive only.
- **Unknown JSON fields and unknown message types are ignored.**
- **Binary messages** start with a layout version byte; a newer version than the receiver
  knows is skipped.

### Specification first

Per CLAUDE.md, `docs/protocol/protocol.md`, JSON Schemas for the JSON messages, binary
layout tables, and golden vectors (JSON files plus hex dumps for binary) land **before** any
code. Firmware (`sarathi_proto`, native tests) and Android (`:core:protocol`, JVM tests) both
test against the same vectors, including malformed and oversized cases.

## Consequences

- **Fast and safe:** about 0.2 KB/s steady state, bursts of seconds. Corrupt or hostile
  frames are dropped by construction.
- **Two encodings to maintain.** Shared vectors keep C++ and Kotlin in step.
- **Clean break from Chronos.** The Chronos build stays until Phase 4 passes its test plan
  (ADR-0005's exit plan).
- **Owner decisions:**
  - the CLAUDE.md rule change (binary for geometry)
  - the 64-fragment message cap
  - the 2 Hz pose and 1 Hz `nav` rates
