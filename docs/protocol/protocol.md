# sārathi phone ↔ device protocol, version 1.1

The protocol between the sārathi Android app ("phone") and the dash ("device"), decided in
ADR-0007. This file is the specification. JSON Schemas for the JSON messages are in
`schema/`, and golden vectors in `vectors/` are what the firmware (`sarathi_proto`) and the
app (`:core:protocol`) test against. `tools/protocol_vectors.py` is the reference encoder
that generates and checks the vectors.

**Implementations,** each tested against every vector:

| Where | What | Tests |
|---|---|---|
| `firmware/lib/sarathi_proto` | `frame` (CRC, fragmenter, reassembler), `json_codec` (decodes phone → device, encodes all), `binary_codec` (decodes pose, route, traffic, chunk into caller buffers; encodes all) | `pio test -e native` (`test_proto_*`) |
| `android/core/protocol` | `Framing`, `Reassembler`, `JsonCodec`, `BinaryCodec` (both directions), a strict JSON parser; no dependencies | `./gradlew testDebugUnitTest` |
| `tools/protocol_vectors.py` | Reference encoder and checker, standard library only | `check-vectors` (CI) |

Both test suites read the files in `vectors/` directly, so CI fails on either side if the
code and the vectors disagree.

**Changing the protocol:** update this file, the schemas and the vectors **first**, then
the code (CLAUDE.md).

Conventions:
- Integers are little-endian.
- *varint* is unsigned LEB128, at most 5 bytes, value < 2³²; a longer or larger varint is an
  error.
- *zigzag* maps a signed value to unsigned (`(n << 1) ^ (n >> 31)`) before varint encoding.
- Lengths are in bytes; text is UTF-8 without a BOM.
- "Drop" means the receiver discards the message, counts it and logs it, and the link stays
  up.

## 1. Transport

### GATT

One primary service:

| Item | UUID | Properties |
|---|---|---|
| Service | `831fd13f-67ea-432e-b7b7-09926c4b92a6` | primary |
| `rx` characteristic (phone → device) | `cd32526c-d882-496f-b7c1-301a1fdf68a7` | Write Without Response, Write |
| `tx` characteristic (device → phone) | `9ae416fd-2b66-43f5-b902-001a23a238a9` | Notify |

- **Security:** both characteristics need an **encrypted, bonded and authenticated** link
  (LE Secure Connections, numeric comparison, confirmed on the device with the knob; see
  `SECURITY.md`). The device ignores writes from any other link.
- **MTU:** the device starts the ATT MTU exchange on connect, asking for 517. Both sides
  must work with an MTU of **247**, so a frame of up to 244 bytes. A smaller MTU is a
  connection the device refuses to use: it logs it and waits for the phone to raise it.
- **Write type:** `hello` from the phone uses Write (with response); every other phone
  frame uses Write Without Response. The device sends with notifications.
- **Phase 5** adds an OTA characteristic; message types `0x60`–`0x6F` are reserved for it.

### Frames

Every write and every notification carries exactly one frame:

```
first fragment   type:u8  msg_id:u8  frag:u8  total_len:u16  crc:u16  payload…
later fragments  type:u8  msg_id:u8  frag:u8                          payload…

frag: bit 7 = FIRST, bit 6 = LAST, bits 0–5 = fragment index (0–63)
```

| Field | Meaning |
|---|---|
| `type` | Message type (section 2). The same in every fragment of a message. |
| `msg_id` | Per direction, it increases by 1 for every message (wraps at 256). The same in every fragment of a message. |
| `frag` | A single-frame message has `FIRST \| LAST` and index 0. A multi-frame message has indices 0, 1, 2, … in order, `FIRST` on index 0 and `LAST` on the final one. |
| `total_len` | Length of the whole message payload, 0–**15,360** bytes. |
| `crc` | CRC-16/CCITT-FALSE of the whole payload (polynomial 0x1021, initial value 0xFFFF, no reflection, no final XOR; `"123456789"` → `0x29B1`). |

**Sending:**
- **Payload sizes:** at most `mtu − 3 − 7` payload bytes in the first fragment and
  `mtu − 3 − 3` in each later one, so at MTU 247 that's 237 and 241.
- **Limits:** at most 64 fragments and 15,360 bytes per message.
- **Size choice:** a sender may make fragments smaller than the maximum but never larger.

**Receiving:**
1. **One multi-frame message at a time per direction.** Single-frame messages (`FIRST|LAST`)
   may arrive between its fragments, and don't disturb it.
2. **A new multi-frame `FIRST` abandons any unfinished one,** counted as `cut_short`.
3. **Drop the message being assembled** when a later fragment has:
   - a different `type` or `msg_id` (`mismatch`)
   - an index other than the next one (`out_of_order`)
   - its payload taking the total past `total_len` (`too_long`)
4. **A later fragment with no message in progress** is dropped (`stray`).
5. **When the `LAST` fragment arrives:**
   - if the received length isn't `total_len`, the message is dropped (`length`)
   - if the CRC doesn't match, it's dropped (`crc`)
   - otherwise the payload goes to the decoder
6. **A first fragment** with `total_len` over 15,360 is dropped (`too_long`), as is a frame
   shorter than its header (`short_frame`).

### Flow control (phone → device)

- **The device grants a window** of frames it can buffer, in a `flow` message (section 3):
  `window` frames, and `consumed`, the number of frames it has taken from its buffer since
  the connection started (u32, wrapping).
- **The phone may send** while `sent − consumed < window`, where `sent` counts its frames
  since the connection started.
- **When the device sends `flow`:**
  - right after its `hello`, with `consumed` = 1 (the phone's `hello`)
  - whenever it has consumed `window / 2` frames since the last `flow`
  - every second while frames are outstanding
- **Before the first `flow`,** the phone sends nothing but its `hello`.
- **No flow control device → phone:** the device's traffic is small, and the phone has
  ample buffering.

## 2. Message types

| Type | Name | Direction | Encoding | Section |
|---|---|---|---|---|
| `0x01` | `hello` | both | JSON | 3 |
| `0x02` | `time` | phone → device | JSON | 3 |
| `0x03` | `phone_batt` | phone → device | JSON | 3 |
| `0x04` | `frame` | phone → device | JSON | 3 |
| `0x05` | `backend` | phone → device | JSON | 3 |
| `0x06` | `event` | phone → device | JSON | 3 |
| `0x07` | `nav` | phone → device | JSON | 3 |
| `0x08` | `nav_end` | phone → device | JSON | 3 |
| `0x09` | `call` | phone → device | JSON | 3 |
| `0x0A` | `media` | phone → device | JSON | 3 |
| `0x0B` | `lock` | phone → device | JSON | 3 (since 1.1) |
| `0x10` | `chunk_ack` | device → phone | JSON | 3 |
| `0x11` | `flow` | device → phone | JSON | 3 |
| `0x12` | `style` | device → phone | JSON | 3 |
| `0x13` | `call_action` | device → phone | JSON | 3 |
| `0x14` | `media_cmd` | device → phone | JSON | 3 |
| `0x40` | `pose` | phone → device | binary | 4 |
| `0x41` | `route` | phone → device | binary | 4 |
| `0x42` | `traffic` | phone → device | binary | 4 |
| `0x43` | `chunk` | phone → device | binary | 4 |
| `0x44` | `chunk_have` | device → phone | binary | 4 |
| `0x60`–`0x6F` | reserved (OTA, Phase 5) | | | |

Unknown types are ignored (versioning, section 6).

## 3. JSON messages

- **Shape:** one JSON object per message, UTF-8, at most 2,048 bytes. Key `"t"` is the
  message name and must match the frame's type; a mismatch is dropped (`type_mismatch`).
- **Unknown keys are ignored.** Missing required keys or values out of range are dropped
  (`invalid`).
- **Text limits are in UTF-8 bytes.** A string over its limit is dropped, never truncated
  by the receiver; the sender truncates on a character boundary. Indian scripts take 3
  bytes per character, so `street` (48 B) holds about 16 Devanagari characters: "महात्मा
  गांधी मार्ग" (53 B) is truncated by the phone.
- The schemas in `schema/<name>.schema.json` are normative for types and ranges. Each uses
  `x-maxBytes` for byte limits (JSON Schema's `maxLength` counts characters).
- Senders never repeat a key in an object, and never send U+0000 or an unpaired surrogate
  in a string; a receiver may drop such a message (`invalid`).
- **Lists of names are sets** (`caps`, `flags`, `layers`): order and repeats carry no meaning.
- **Canonical encoding.** Receivers accept any valid JSON, but every sender writes the
  canonical form, so both implementations are tested byte for byte against the same
  vectors (`canonical` in `vectors/json_messages.json`):
  - only the keys the schema defines, in the order of the tables below (the schema's
    `properties` order), nested objects too; `null` written out where allowed;
  - sets in the order of the schema's `enum`, each value once;
  - no whitespace; integers in plain decimal;
  - strings as raw UTF-8, escaping only `"` and `\` (as `\"`, `\\`) and U+0000–U+001F
    (`\b`, `\t`, `\n`, `\f`, `\r`, otherwise `\u00xx` in lower-case hex).
- **Text the device shows** (`street`, `caller`, `title`, `artist`, `name`, `attribution`):
  the protocol carries UTF-8, but the device's fonts cover only printable ASCII plus `°` and
  `•` (LVGL's built-in Montserrat, ADR-0003). The phone therefore asks its
  backends for English or Latin-script names where it can, strips accents (`é` → `e`) and
  replaces `©` with `(c)`; text with nothing left to show (an all-Devanagari street) is sent as
  an empty string. Any other character shows as LVGL's placeholder box on the device.

### Phone → device

| Message | Fields | When |
|---|---|---|
| `hello` | `proto` [major, minor] = [1, 1]; `app` app version (≤ 32 B); `caps` list of strings from `google`, `mappls`, `osm`, `offline`, `traffic`, `calls`, `media` | First message after connecting (Write with response) |
| `time` | `local` local time `YYYY-MM-DDTHH:MM:SS`; `utc_offset_min` −720…840 | After `hello`, then every 10 min |
| `phone_batt` | `level` 0–100; `charging` bool | After `hello`, then on change |
| `frame` | `epoch` 0–65,535 | At ride start and when re-anchoring (ADR-0009) |
| `backend` | `routing` one of `google`, `mappls`, `osm_online`, `osm_offline`; `search` one of `google`, `mappls`, `osm`; `name` display name (≤ 24 B); `attribution` (≤ 64 B); `traffic` bool; `two_wheeler` bool | At ride start and when it changes |
| `event` | `kind` `fallback` (+ `from`, `to`: routing backend ids; `reason`: `quota`, `offline`, `error`, `no_route`, `account`) or `reroute` or `cost_warning` (+ `backend`, `percent` 0–100) | When it happens |
| `nav` | `m` maneuver name (table below); `exit` 0–15 (roundabout exit, 0 if none); `dist_m` 0–1,000,000 to the maneuver; `street` next street (≤ 48 B, may be empty); `rem_s` traffic-aware remaining time 0–604,800; `rem_m` remaining distance 0–10,000,000; `flags` list from `off_route`, `rerouting`, `arrived` | 1 Hz while navigating, and on change |
| `nav_end` | `reason` `arrived`, `cancelled` or `error` | When navigation ends |
| `call` | `phase` `ringing`, `active` or `idle`; `caller` (≤ 32 B, may be empty) | On change |
| `media` | `avail` bool; `playing` bool; `title` (≤ 48 B); `artist` (≤ 48 B); `pos_s` 0–86,400; `dur_s` 0–86,400 (0 = unknown) | On change, and every 5 s while playing |
| `lock` (1.1) | The moving lock (settings locked while moving, section 5): `enabled` bool, **false turns the lock off entirely** (the owner's choice, ADR-0011); `move_kmh` 1–50 and `move_s` 0–30: moving once `pose` speed is at least `move_kmh` for `move_s`; `stop_kmh` 0–49, **below `move_kmh`**, and `stop_s` 0–60: stopped once speed is below `stop_kmh` for `stop_s` | After `hello`, and on change |

**Maneuvers:** the `m` strings, with numeric codes used by the binary `route` message.
Codes 12–19 have no glyph of their own yet; the device draws the fallback until they do.

| Code | `m` | Device fallback | | Code | `m` | Device fallback |
|---|---|---|---|---|---|---|
| 0 | `none` | — | | 10 | `roundabout` | — |
| 1 | `depart` | — | | 11 | `arrive` | — |
| 2 | `straight` | — | | 12 | `keep_left` | `slight_left` |
| 3 | `slight_left` | — | | 13 | `keep_right` | `slight_right` |
| 4 | `left` | — | | 14 | `merge` | `straight` |
| 5 | `sharp_left` | — | | 15 | `fork_left` | `slight_left` |
| 6 | `slight_right` | — | | 16 | `fork_right` | `slight_right` |
| 7 | `right` | — | | 17 | `ramp_left` | `slight_left` |
| 8 | `sharp_right` | — | | 18 | `ramp_right` | `slight_right` |
| 9 | `uturn` | — | | 19 | `ferry` | `straight` |

An unknown `m` (a newer minor version) is drawn as `straight`.

### Device → phone

| Message | Fields | When |
|---|---|---|
| `hello` | `proto` [1, 1]; `fw` firmware version (≤ 16 B); `seq` firmware seq; `caps`: `styles` (list of map style names), `chunk_kb` chunk cache size, `max_msg` (15,360); `route`: `{ "id", "v" }` of the route it holds, or `null`; `epoch` it holds, or `null` | Reply to the phone's `hello` |
| `flow` | `window` 1–64 frames; `consumed` u32 | See section 1 |
| `style` | `map` active map style (`classic`, `hud`, `racing`, `minimal`); `layers` list from `water`, `parks` | After `hello`, then on change |
| `chunk_ack` | `stored` list of keys `[level, tile_x, tile_y, data_version]`; `refused` list of `[level, tile_x, tile_y, data_version, reason]`, reason `full`, `invalid`, `epoch` | After receiving chunks (batched, at most every 500 ms) |
| `call_action` | `action` `accept` or `reject` | Knob press on the call screen |
| `media_cmd` | `cmd` `play_pause`, `next` or `prev` | Knob press on the music screen |

## 4. Binary messages

- **Header:** every binary message starts with a layout version byte, `ver` = **1**. A
  receiver skips a message whose `ver` is newer than it knows (`ver_unknown`).
- **Validity:** a message is dropped if it is shorter than its fields, has bytes left over,
  or breaks a limit (`invalid`).

### Coordinates

- **Positions** are in the **ride frame** (ADR-0009): x east and y north, relative to an
  origin the phone chooses per ride. The device never receives latitude or longitude.
- **Epochs:** every positional message carries the frame `epoch`. A message whose epoch
  differs from the device's current one is ignored (`epoch`), and `chunk` is refused with
  reason `epoch`.

### `pose` (0x40): 20 bytes

| Offset | Field | Type | Meaning |
|---|---|---|---|
| 0 | `ver` | u8 | 1 |
| 1 | `epoch` | u16 | Ride frame |
| 3 | `t_ms` | u32 | Phone monotonic time, ms (wraps) |
| 7 | `x_dm` | i32 | x, decimetres |
| 11 | `y_dm` | i32 | y, decimetres |
| 15 | `heading_cdeg` | u16 | Direction of travel, 0.01° clockwise from north, 0–35,999 |
| 17 | `speed_cms` | u16 | Speed, cm/s |
| 19 | `acc_m` | u8 | Horizontal accuracy, m (255 = unknown) |

### `route` (0x41)

| Field | Type | Meaning |
|---|---|---|
| `ver` | u8 | 1 |
| `route_id` | u32 | Changes with every new route |
| `version` | u16 | Changes with every reroute of the same trip |
| `backend` | u8 | 0 `google`, 1 `mappls`, 2 `osm_online`, 3 `osm_offline` |
| `epoch` | u16 | Ride frame |
| `mode` | u8 | 0 two-wheeler, 1 driving |
| `quantum_dm` | u8 | Coordinate unit, decimetres, 1–255 (10 = 1 m) |
| `n_points` | varint | 2–4,000 |
| points | zigzag varint pairs | The first point is x, y in quanta from the ride-frame origin; each later one is the delta from the previous point. Every point lies within −2³⁰…2³⁰−1 on both axes, so every delta fits a 32-bit zigzag. |
| `n_markers` | varint | 0–256 |
| markers | `point` varint, `maneuver` u8, `exit` u8 | `point` < `n_points`, in increasing order; `maneuver` is a code from the table in section 3 |

### `traffic` (0x42)

| Field | Type | Meaning |
|---|---|---|
| `ver` | u8 | 1 |
| `route_id` | u32 | Route it applies to |
| `version` | u16 | Route version it applies to; any other route is ignored (`stale`) |
| `n` | varint | 0–256 |
| entries | `offset_m` varint, `length_m` varint, `level` u8 | Stretches along the route, in metres from its start, sorted by offset and not overlapping. Levels: 0 free, 1 slow, 2 jam, 3 unknown. Parts of the route not covered are **unknown**. A sender with free-flow information (Google) sends free stretches explicitly. |

### `chunk` (0x43)

| Field | Type | Meaning |
|---|---|---|
| `ver` | u8 | 1 |
| `level` | u8 | 0 = L0 (z14 tile), 1 = L1 (z12 tile) |
| `tile_x`, `tile_y` | u32, u32 | Tile coordinates at the level's zoom |
| `data_version` | u32 | Version of the source data (for example `20261018` for the OpenFreeMap build of that day) |
| `provenance` | u8 | 1 `osm_openfreemap`, 2 `osm_protomaps_offline`. 0 is invalid. Only OSM-derived data is ever sent as a chunk. |
| `epoch` | u16 | Ride frame |
| `origin_x_m`, `origin_y_m` | i32, i32 | The chunk's local origin in the ride frame, metres |
| `quantum_dm` | u8 | Coordinate unit, decimetres (20 for L0, 80 for L1) |
| `extent` | u16 | Chunk size in quanta (coordinates may stray up to 64 quanta outside after clipping) |
| `n_lines` | varint | 0–4,096 |
| lines | `class` u8, `n` varint (2–1,024), points as zigzag varint pairs | Roads. Class 0 `motorway`, 1 `trunk`, 2 `primary`, 3 `secondary`, 4 `tertiary`, 5 `minor`, 6 `service`. The first point is relative to the chunk origin; the others are deltas. |
| `n_polys` | varint | 0–1,024 |
| polygons | `kind` u8, `n` varint (3–1,024), points as zigzag varint pairs | Kind 0 water, 1 park. One closed outer ring; the last point isn't repeated. |

Limits across a chunk: at most 8,192 points in total, within the 15,360-byte message limit.
The phone splits a denser tile into more chunks, at the next zoom level.

### `chunk_have` (0x44, device → phone)

| Field | Type | Meaning |
|---|---|---|
| `ver` | u8 | 1 |
| `epoch` | u16 | The device's current ride frame (0 if none) |
| `n` | varint | 0–512 |
| entries | `level` u8, `tile_x` u32, `tile_y` u32, `data_version` u32 | The chunks it holds |

## 5. Sequences

**Connect:**
1. The link is bonded and encrypted; the device starts the MTU exchange.
2. Phone sends `hello`. Device answers `hello`, `flow`, `style`, and then `chunk_have` if
   it holds any chunks.
3. Phone sends `time` and `phone_batt`. If a ride is active, it also sends:
   - `frame` (if the device's epoch differs)
   - `backend`
   - `route` (unless the device holds the same `route_id` and `version`)
   - `traffic`
   - `nav` and `pose`
   - missing chunks for the corridor, nearest first

**Ride start:** `frame`, `backend`, `route`, `traffic`, `nav`, `pose`, then chunks.

**Reroute:** `event` (`reroute`), a new `route` (same `route_id`, new `version`), then
`traffic` and `nav`.

**Re-anchor:** a new `frame` epoch. The device drops its chunks and route; the phone resends
what's near.

**Ride end:** `nav_end`. The device keeps its chunks until the next `frame`.

**Phone scheduling, highest priority first:**
1. `nav`, `call`, `pose`
2. `route`, `traffic`, `event`, `backend`, `frame`
3. `time`, `phone_batt`, `media`
4. `chunk`

Single-frame messages are sent between the fragments of a multi-frame `chunk`. A `route`
may abandon an unfinished `chunk` by starting its own multi-frame message.

**Moving** (the settings lock, CLAUDE.md "Rider safety"): the device follows `pose` speed
with the `lock` thresholds, or until a `lock` arrives the defaults: moving at 5 km/h for 2 s,
stopped below 3 km/h for 5 s, enabled. With no `pose` for 5 s, it falls back to the rule for
a source without speed: settings locked while navigating. With `enabled` false, settings never
lock for movement.

**Device staleness rules** (shown on screen, never silent):
- **No `pose`:** for 3 s shows "Position stale"; dead-reckoning stops after 5 s.
- **No `nav`:** for 10 s while navigating shows "Navigation stale".
- **Traffic** older than 5 minutes is drawn as unknown.
- **A disconnect** shows that the phone is gone ("No phone").

## 6. Versioning

- **`proto` = [major, minor],** separate from the firmware and app versions.
- **Major mismatch:** the device shows "Update the app" or "Update the firmware" and
  ignores everything but `hello`.
- **Minor versions only add:** new message types, new optional JSON keys, new enum values.
  Receivers ignore unknown types and keys, and handle unknown enum values as each table
  says.
- **Binary layouts change only with a new `ver` byte.**
- **History:** 1.0 (2026-10-31), the first version. 1.1 (2026-10-31) adds `lock`; a 1.0 peer
  ignores it and the device keeps its defaults.

## 7. Rates (typical)

| Message | Rate |
|---|---|
| `pose` | 2 Hz while navigating; 1 Hz otherwise |
| `nav` | 1 Hz, plus on change |
| `traffic` | On change, at most every 30 s |
| `media` | On change; every 5 s while playing |
| `time` | Every 10 min |
| `chunk` | As needed, lowest priority |

Measured budget (ADR-0009): about 0.2 KB/s steady state.
