#!/usr/bin/env python3
"""sārathi phone ↔ device protocol v1: reference encoder and golden vectors
(docs/protocol/protocol.md, ADR-0007).

This is the reference for the rules in the spec: framing (fragments, CRC-16, reassembly and
its drop reasons), JSON message validation against docs/protocol/schema/, and the binary
layouts (pose, route, traffic, chunk, chunk_have). The firmware (sarathi_proto) and the app
(:core:protocol) are tested against the vectors it writes.

Commands:
  make-vectors DIR    write the golden vectors (deterministic)
  check-vectors DIR   check every vector against this implementation

Standard library only; run from the project .venv.
"""

from __future__ import annotations

import argparse
import json
import re
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SCHEMA_DIR = ROOT / "docs" / "protocol" / "schema"

# ---- Constants from protocol.md ----
FIRST = 0x80
LAST = 0x40
MAX_FRAGMENTS = 64
MAX_MESSAGE = 15360
MAX_JSON = 2048
FIRST_HEADER = 7
NEXT_HEADER = 3

TYPES = {
    0x01: "hello", 0x02: "time", 0x03: "phone_batt", 0x04: "frame", 0x05: "backend",
    0x06: "event", 0x07: "nav", 0x08: "nav_end", 0x09: "call", 0x0A: "media", 0x0B: "lock",
    0x10: "chunk_ack", 0x11: "flow", 0x12: "style", 0x13: "call_action", 0x14: "media_cmd",
    0x40: "pose", 0x41: "route", 0x42: "traffic", 0x43: "chunk", 0x44: "chunk_have",
}
CODES = {v: k for k, v in TYPES.items()}
BINARY = {"pose", "route", "traffic", "chunk", "chunk_have"}


# ---- CRC-16/CCITT-FALSE ----

def crc16(data: bytes) -> int:
    crc = 0xFFFF
    for byte in data:
        crc ^= byte << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) if crc & 0x8000 else (crc << 1)
            crc &= 0xFFFF
    return crc


# ---- Framing ----

def fragment(msg_type: int, msg_id: int, payload: bytes, mtu: int = 247) -> list[bytes]:
    """Splits a message into frames for an ATT MTU (protocol.md section 1)."""
    if len(payload) > MAX_MESSAGE:
        raise ValueError("message too long")
    first_cap = mtu - 3 - FIRST_HEADER
    next_cap = mtu - 3 - NEXT_HEADER
    chunks = [payload[:first_cap]]
    rest = payload[first_cap:]
    while rest:
        chunks.append(rest[:next_cap])
        rest = rest[next_cap:]
    if len(chunks) > MAX_FRAGMENTS:
        raise ValueError("too many fragments")
    frames = []
    for index, chunk in enumerate(chunks):
        flags = index
        if index == 0:
            flags |= FIRST
        if index == len(chunks) - 1:
            flags |= LAST
        header = bytes([msg_type, msg_id & 0xFF, flags])
        if index == 0:
            header += struct.pack("<HH", len(payload), crc16(payload))
        frames.append(header + chunk)
    return frames


class Reassembler:
    """Receiver rules of protocol.md section 1. feed() returns a list of events:
    ("message", type, msg_id, payload) or ("drop", reason)."""

    def __init__(self) -> None:
        self.active = None  # [type, msg_id, next_index, total_len, crc, bytearray]

    def feed(self, frame: bytes) -> list[tuple]:
        events: list[tuple] = []
        if len(frame) < NEXT_HEADER:
            return [("drop", "short_frame")]
        msg_type, msg_id, flags = frame[0], frame[1], frame[2]
        index = flags & 0x3F
        first, last = bool(flags & FIRST), bool(flags & LAST)
        if first:
            if len(frame) < FIRST_HEADER:
                return [("drop", "short_frame")]
            total_len, crc = struct.unpack_from("<HH", frame, 3)
            if index != 0:
                return [("drop", "out_of_order")]
            if total_len > MAX_MESSAGE:
                return [("drop", "too_long")]
            body = frame[FIRST_HEADER:]
            if last:  # single-frame message: never disturbs one in progress
                return self._complete(msg_type, msg_id, total_len, crc, bytes(body))
            if self.active is not None:
                events.append(("drop", "cut_short"))
            if len(body) > total_len:
                self.active = None
                return events + [("drop", "too_long")]
            self.active = [msg_type, msg_id, 1, total_len, crc, bytearray(body)]
            return events
        if self.active is None:
            return [("drop", "stray")]
        a_type, a_id, a_next, a_total, a_crc, buf = self.active
        if msg_type != a_type or msg_id != a_id:
            self.active = None
            return [("drop", "mismatch")]
        if index != a_next:
            self.active = None
            return [("drop", "out_of_order")]
        body = frame[NEXT_HEADER:]
        if len(buf) + len(body) > a_total:
            self.active = None
            return [("drop", "too_long")]
        buf += body
        self.active[2] = a_next + 1
        if not last:
            return []
        self.active = None
        return self._complete(a_type, a_id, a_total, a_crc, bytes(buf))

    @staticmethod
    def _complete(msg_type, msg_id, total_len, crc, payload) -> list[tuple]:
        if len(payload) != total_len:
            return [("drop", "length")]
        if crc16(payload) != crc:
            return [("drop", "crc")]
        return [("message", msg_type, msg_id, payload)]


# ---- JSON validation (the subset of JSON Schema the schemas use) ----

class Invalid(Exception):
    pass


def _check(value, schema: dict, path: str) -> None:
    if "oneOf" in schema:
        ok = 0
        for sub in schema["oneOf"]:
            try:
                _check(value, sub, path)
                ok += 1
            except Invalid:
                pass
        if ok != 1:
            raise Invalid(f"{path}: oneOf")
    for cond in schema.get("allOf", []):
        try:
            _check(value, cond["if"], path)
            matched = True
        except Invalid:
            matched = False
        if matched:
            _check(value, cond["then"], path)
    if "const" in schema and value != schema["const"]:
        raise Invalid(f"{path}: const")
    if "enum" in schema and value not in schema["enum"]:
        raise Invalid(f"{path}: enum")
    kind = schema.get("type")
    if kind == "object" and not isinstance(value, dict):
        raise Invalid(f"{path}: object")
    if isinstance(value, dict) and (kind == "object" or "properties" in schema or "required" in schema):
        for key in schema.get("required", []):
            if key not in value:
                raise Invalid(f"{path}.{key}: required")
        for key, sub in schema.get("properties", {}).items():
            if key in value:
                _check(value[key], sub, f"{path}.{key}")
    if kind == "array":
        if not isinstance(value, list):
            raise Invalid(f"{path}: array")
        if len(value) < schema.get("minItems", 0) or len(value) > schema.get("maxItems", 1 << 30):
            raise Invalid(f"{path}: items count")
        prefix = schema.get("prefixItems", [])
        for n, item in enumerate(value):
            if n < len(prefix):
                _check(item, prefix[n], f"{path}[{n}]")
            elif schema.get("items") is False:
                raise Invalid(f"{path}: extra items")
            elif isinstance(schema.get("items"), dict):
                _check(item, schema["items"], f"{path}[{n}]")
    elif kind == "string":
        if not isinstance(value, str):
            raise Invalid(f"{path}: string")
        if len(value.encode()) > schema.get("x-maxBytes", 1 << 30):
            raise Invalid(f"{path}: too many bytes")
        if "pattern" in schema and not re.search(schema["pattern"], value):
            raise Invalid(f"{path}: pattern")
    elif kind == "integer":
        if not isinstance(value, int) or isinstance(value, bool):
            raise Invalid(f"{path}: integer")
        if value < schema.get("minimum", -(1 << 63)) or value > schema.get("maximum", 1 << 63):
            raise Invalid(f"{path}: range")
    elif kind == "boolean":
        if not isinstance(value, bool):
            raise Invalid(f"{path}: boolean")
    elif kind == "null":
        if value is not None:
            raise Invalid(f"{path}: null")


def schema_for(name: str, direction: str) -> dict:
    file = f"hello.{direction}" if name == "hello" else name
    return json.loads((SCHEMA_DIR / f"{file}.schema.json").read_text())


def validate_json(msg_type: int, payload: bytes, direction: str) -> dict:
    """Returns the decoded object, or raises Invalid with a reason."""
    if len(payload) > MAX_JSON:
        raise Invalid("too_long")
    try:
        obj = json.loads(payload.decode("utf-8"))
    except (UnicodeDecodeError, json.JSONDecodeError):
        raise Invalid("not_json") from None
    if not isinstance(obj, dict) or obj.get("t") != TYPES.get(msg_type):
        raise Invalid("type_mismatch")
    try:
        _check(obj, schema_for(TYPES[msg_type], direction), "$")
    except Invalid as e:
        raise Invalid(f"invalid ({e})") from None
    # Rules a schema can't state (protocol.md section 3).
    if obj["t"] == "lock" and obj["stop_kmh"] >= obj["move_kmh"]:
        raise Invalid("invalid ($.stop_kmh: below move_kmh)")
    return obj


# ---- Binary encoding ----

def varint(value: int) -> bytes:
    if value < 0 or value >= 1 << 32:
        raise ValueError("varint range")
    out = bytearray()
    while True:
        b = value & 0x7F
        value >>= 7
        out.append(b | (0x80 if value else 0))
        if not value:
            return bytes(out)


def zigzag(n: int) -> int:
    if n < -(1 << 31) or n >= 1 << 31:
        raise ValueError("zigzag range")
    return ((n << 1) ^ (n >> 31)) & 0xFFFFFFFF


def unzigzag(u: int) -> int:
    return (u >> 1) ^ -(u & 1)


class Reader:
    def __init__(self, data: bytes) -> None:
        self.data, self.pos = data, 0

    def take(self, fmt: str):
        size = struct.calcsize(fmt)
        if self.pos + size > len(self.data):
            raise Invalid("invalid (short)")
        values = struct.unpack_from(fmt, self.data, self.pos)
        self.pos += size
        return values if len(values) > 1 else values[0]

    def varint(self) -> int:
        value = shift = 0
        for n in range(5):
            if self.pos >= len(self.data):
                raise Invalid("invalid (short)")
            b = self.data[self.pos]
            self.pos += 1
            value |= (b & 0x7F) << shift
            if not b & 0x80:
                if value >= 1 << 32:
                    raise Invalid("invalid (varint)")
                return value
            shift += 7
        raise Invalid("invalid (varint)")

    def svarint(self) -> int:
        return unzigzag(self.varint())

    def done(self) -> None:
        if self.pos != len(self.data):
            raise Invalid("invalid (trailing)")


def _points(points: list[list[int]]) -> bytes:
    out = bytearray()
    px = py = 0
    for x, y in points:
        out += varint(zigzag(x - px)) + varint(zigzag(y - py))
        px, py = x, y
    return bytes(out)


def _read_points(r: Reader, n: int) -> list[list[int]]:
    pts, x, y = [], 0, 0
    for _ in range(n):
        x += r.svarint()
        y += r.svarint()
        pts.append([x, y])
    return pts


ROUTE_LIMIT = 1 << 30  # route coordinates, quanta (protocol.md section 4)


def _limit(cond: bool, what: str) -> None:
    if not cond:
        raise Invalid(f"invalid ({what})")


def encode_binary(name: str, m: dict) -> bytes:
    if name == "pose":
        return struct.pack("<BHIiiHHB", 1, m["epoch"], m["t_ms"], m["x_dm"], m["y_dm"],
                           m["heading_cdeg"], m["speed_cms"], m["acc_m"])
    if name == "route":
        out = struct.pack("<BIHBHBB", 1, m["route_id"], m["version"], m["backend"], m["epoch"],
                          m["mode"], m["quantum_dm"])
        out += varint(len(m["points"])) + _points(m["points"]) + varint(len(m["markers"]))
        for p, man, ex in m["markers"]:
            out += varint(p) + bytes([man, ex])
        return out
    if name == "traffic":
        out = struct.pack("<BIH", 1, m["route_id"], m["version"]) + varint(len(m["entries"]))
        for off, length, level in m["entries"]:
            out += varint(off) + varint(length) + bytes([level])
        return out
    if name == "chunk":
        out = struct.pack("<BBIIIBHiiBH", 1, m["level"], m["tile_x"], m["tile_y"],
                          m["data_version"], m["provenance"], m["epoch"], m["origin_x_m"],
                          m["origin_y_m"], m["quantum_dm"], m["extent"])
        out += varint(len(m["lines"]))
        for cls, pts in m["lines"]:
            out += bytes([cls]) + varint(len(pts)) + _points(pts)
        out += varint(len(m["polys"]))
        for kind, pts in m["polys"]:
            out += bytes([kind]) + varint(len(pts)) + _points(pts)
        return out
    if name == "chunk_have":
        out = struct.pack("<BH", 1, m["epoch"]) + varint(len(m["entries"]))
        for level, tx, ty, ver in m["entries"]:
            out += struct.pack("<BIII", level, tx, ty, ver)
        return out
    raise ValueError(name)


def decode_binary(name: str, data: bytes) -> dict:
    r = Reader(data)
    if not data:
        raise Invalid("invalid (short)")
    if data[0] != 1:
        raise Invalid("ver_unknown")
    if name == "pose":
        _, epoch, t_ms, x, y, hdg, spd, acc = r.take("<BHIiiHHB")
        r.done()
        _limit(hdg <= 35999, "heading")
        return {"epoch": epoch, "t_ms": t_ms, "x_dm": x, "y_dm": y, "heading_cdeg": hdg,
                "speed_cms": spd, "acc_m": acc}
    if name == "route":
        _, rid, ver, backend, epoch, mode, q = r.take("<BIHBHBB")
        _limit(backend <= 3 and mode <= 1 and q >= 1, "header")
        n = r.varint()
        _limit(2 <= n <= 4000, "n_points")
        pts = _read_points(r, n)
        _limit(all(-ROUTE_LIMIT <= c < ROUTE_LIMIT for p in pts for c in p), "coordinates")
        nm = r.varint()
        _limit(nm <= 256, "n_markers")
        markers, prev = [], -1
        for _ in range(nm):
            p = r.varint()
            man, ex = r.take("<BB")
            _limit(prev < p < n, "marker order")
            prev = p
            markers.append([p, man, ex])
        r.done()
        return {"route_id": rid, "version": ver, "backend": backend, "epoch": epoch,
                "mode": mode, "quantum_dm": q, "points": pts, "markers": markers}
    if name == "traffic":
        _, rid, ver = r.take("<BIH")
        n = r.varint()
        _limit(n <= 256, "n")
        entries, end = [], 0
        for _ in range(n):
            off, length = r.varint(), r.varint()
            level = r.take("<B")
            _limit(level <= 3 and off >= end and length > 0, "entry")
            end = off + length
            entries.append([off, length, level])
        r.done()
        return {"route_id": rid, "version": ver, "entries": entries}
    if name == "chunk":
        (_, level, tx, ty, dver, prov, epoch, ox, oy, q, extent) = r.take("<BBIIIBHiiBH")
        _limit(level <= 1 and prov in (1, 2) and q >= 1 and extent >= 1, "header")
        total = 0
        nl = r.varint()
        _limit(nl <= 4096, "n_lines")
        lines = []
        for _ in range(nl):
            cls = r.take("<B")
            n = r.varint()
            _limit(cls <= 6 and 2 <= n <= 1024, "line")
            total += n
            _limit(total <= 8192, "points")
            pts = _read_points(r, n)
            _limit(all(-64 <= c <= extent + 64 for p in pts for c in p), "coordinates")
            lines.append([cls, pts])
        npoly = r.varint()
        _limit(npoly <= 1024, "n_polys")
        polys = []
        for _ in range(npoly):
            kind = r.take("<B")
            n = r.varint()
            _limit(kind <= 1 and 3 <= n <= 1024, "polygon")
            total += n
            _limit(total <= 8192, "points")
            pts = _read_points(r, n)
            _limit(all(-64 <= c <= extent + 64 for p in pts for c in p), "coordinates")
            polys.append([kind, pts])
        r.done()
        return {"level": level, "tile_x": tx, "tile_y": ty, "data_version": dver,
                "provenance": prov, "epoch": epoch, "origin_x_m": ox, "origin_y_m": oy,
                "quantum_dm": q, "extent": extent, "lines": lines, "polys": polys}
    if name == "chunk_have":
        _, epoch = r.take("<BH")
        n = r.varint()
        _limit(n <= 512, "n")
        entries = []
        for _ in range(n):
            level, tx, ty, ver = r.take("<BIII")
            _limit(level <= 1, "level")
            entries.append([level, tx, ty, ver])
        r.done()
        return {"epoch": epoch, "entries": entries}
    raise ValueError(name)


# ---- Vectors ----

def _json_bytes(obj: dict) -> bytes:
    return json.dumps(obj, separators=(",", ":"), ensure_ascii=False).encode()


def _canonical_value(value, schema: dict):
    if isinstance(value, dict):
        props = schema.get("properties", {})
        if not props:  # oneOf (hello.route): use the object branch
            props = next((s["properties"] for s in schema.get("oneOf", []) if "properties" in s), {})
        return {k: _canonical_value(value[k], props[k]) for k in props if k in value}
    if isinstance(value, list):
        items = schema.get("items")
        if isinstance(items, dict) and "enum" in items:  # a set: enum order, each once
            return [e for e in items["enum"] if e in value]
        return list(value)
    return value


def canonical_json(name: str, direction: str, obj: dict) -> bytes:
    """The canonical encoding (protocol.md section 3): only the keys the schema knows, in
    schema order (the order of protocol.md's tables); sets in enum order, each value once;
    compact; raw UTF-8, escaping only quote, backslash and U+0000-U+001F."""
    return _json_bytes(_canonical_value(obj, schema_for(name, direction)))


def _city_chunk() -> dict:
    """A synthetic dense L0 chunk, deterministic: a street grid with bends, like a city
    centre (about the size of the Pune chunk measured for ADR-0009)."""
    lines = []
    for i in range(36):
        cls = 2 if i % 9 == 0 else (4 if i % 3 == 0 else 5)
        pts_v = [[i * 32 + (j % 3) * 2, j * 14] for j in range(80)]
        pts_h = [[j * 14, i * 32 + (j % 4)] for j in range(80)]
        lines.append([cls, pts_v])
        lines.append([cls if cls != 2 else 3, pts_h])
    polys = [[0, [[10, 10], [200, 10], [220, 120], [30, 140]]],
             [1, [[600, 600], [700, 600], [700, 700], [600, 700]]]]
    return {"level": 0, "tile_x": 11553, "tile_y": 7334, "data_version": 20261018, "provenance": 1,
            "epoch": 7, "origin_x_m": -1160, "origin_y_m": 1160, "quantum_dm": 20, "extent": 1160,
            "lines": lines, "polys": polys}


JSON_VALID = [
    ("hello-phone", "phone", {"t": "hello", "proto": [1, 0], "app": "0.1.0", "caps": ["google", "traffic", "calls", "media"]}),
    ("hello-device", "device", {"t": "hello", "proto": [1, 0], "fw": "0.5.0", "seq": 500,
                                "caps": {"styles": ["classic", "hud", "racing", "minimal"], "chunk_kb": 1536, "max_msg": 15360},
                                "route": {"id": 3, "v": 2}, "epoch": 7}),
    ("hello-device-empty", "device", {"t": "hello", "proto": [1, 0], "fw": "0.5.0", "seq": 500,
                                      "caps": {"styles": ["classic"], "chunk_kb": 1536, "max_msg": 15360}, "route": None, "epoch": None}),
    ("time", "phone", {"t": "time", "local": "2026-10-31T18:42:05", "utc_offset_min": 330}),
    ("phone-batt", "phone", {"t": "phone_batt", "level": 77, "charging": False}),
    ("frame", "phone", {"t": "frame", "epoch": 7}),
    ("backend-google", "phone", {"t": "backend", "routing": "google", "search": "google", "name": "Google",
                                 "attribution": "Google", "traffic": True, "two_wheeler": True}),
    ("backend-osm-offline", "phone", {"t": "backend", "routing": "osm_offline", "search": "osm", "name": "OSM (offline)",
                                      "attribution": "© OpenStreetMap contributors", "traffic": False, "two_wheeler": False}),
    ("event-fallback", "phone", {"t": "event", "kind": "fallback", "from": "google", "to": "mappls", "reason": "quota"}),
    ("event-reroute", "phone", {"t": "event", "kind": "reroute"}),
    ("event-cost", "phone", {"t": "event", "kind": "cost_warning", "backend": "google", "percent": 80}),
    ("nav", "phone", {"t": "nav", "m": "left", "exit": 0, "dist_m": 250, "street": "MG Road", "rem_s": 540, "rem_m": 4200, "flags": []}),
    ("nav-roundabout", "phone", {"t": "nav", "m": "roundabout", "exit": 2, "dist_m": 80, "street": "", "rem_s": 300,
                                 "rem_m": 2100, "flags": ["rerouting"]}),
    ("nav-unknown-maneuver", "phone", {"t": "nav", "m": "teleport", "exit": 0, "dist_m": 10, "street": "", "rem_s": 1,
                                       "rem_m": 10, "flags": [], "future_key": 1}),
    ("nav-devanagari", "phone", {"t": "nav", "m": "right", "exit": 0, "dist_m": 400, "street": "गांधी मार्ग",
                                 "rem_s": 600, "rem_m": 5000, "flags": []}),
    ("nav-flags-unordered", "phone", {"t": "nav", "m": "arrive", "exit": 0, "dist_m": 0, "street": "", "rem_s": 0,
                                      "rem_m": 0, "flags": ["arrived", "off_route", "arrived"]}),
    ("nav-end", "phone", {"t": "nav_end", "reason": "arrived"}),
    ("call", "phone", {"t": "call", "phase": "ringing", "caller": "Contact A"}),
    ("call-escapes", "phone", {"t": "call", "phase": "active", "caller": "Dr \"K\" \\ A\tB\u0001"}),
    ("media", "phone", {"t": "media", "avail": True, "playing": True, "title": "Kesariya", "artist": "Arijit Singh",
                        "pos_s": 25, "dur_s": 268}),
    ("lock-default", "phone", {"t": "lock", "enabled": True, "move_kmh": 5, "move_s": 2, "stop_kmh": 3, "stop_s": 5}),
    ("lock-off", "phone", {"t": "lock", "enabled": False, "move_kmh": 10, "move_s": 0, "stop_kmh": 0, "stop_s": 60}),
    ("chunk-ack", "device", {"t": "chunk_ack", "stored": [[0, 11553, 7334, 20261018]], "refused": [[1, 2888, 1833, 20261018, "full"]]}),
    ("flow", "device", {"t": "flow", "window": 32, "consumed": 1}),
    ("style", "device", {"t": "style", "map": "hud", "layers": ["water"]}),
    ("call-action", "device", {"t": "call_action", "action": "reject"}),
    ("media-cmd", "device", {"t": "media_cmd", "cmd": "next"}),
]

JSON_INVALID = [
    ("missing-field", "phone", 0x07, '{"t":"nav","m":"left","exit":0,"dist_m":250,"street":"","rem_s":1,"rem_m":1}', "invalid"),
    ("out-of-range", "phone", 0x03, '{"t":"phone_batt","level":101,"charging":true}', "invalid"),
    ("wrong-type", "phone", 0x03, '{"t":"phone_batt","level":"77","charging":true}', "invalid"),
    ("street-too-long", "phone", 0x07, json.dumps({"t": "nav", "m": "left", "exit": 0, "dist_m": 1, "street": "मार्ग" * 4,
                                                    "rem_s": 1, "rem_m": 1, "flags": []}, ensure_ascii=False), "invalid"),
    ("type-mismatch", "phone", 0x09, '{"t":"media","avail":false,"playing":false,"title":"","artist":"","pos_s":0,"dur_s":0}', "type_mismatch"),
    ("not-json", "phone", 0x07, '{"t":"nav",', "not_json"),
    ("not-object", "phone", 0x04, '[7]', "type_mismatch"),
    ("bad-enum", "device", 0x13, '{"t":"call_action","action":"answer"}', "invalid"),
    ("fallback-without-reason", "phone", 0x06, '{"t":"event","kind":"fallback","from":"google","to":"mappls"}', "invalid"),
    ("lock-stop-not-below-move", "phone", 0x0B,
     '{"t":"lock","enabled":true,"move_kmh":5,"move_s":2,"stop_kmh":5,"stop_s":5}', "invalid"),
    ("lock-too-fast", "phone", 0x0B,
     '{"t":"lock","enabled":true,"move_kmh":51,"move_s":2,"stop_kmh":3,"stop_s":5}', "invalid"),
    ("bad-time", "phone", 0x02, '{"t":"time","local":"31/10/2026 18:42","utc_offset_min":330}', "invalid"),
    ("too-long", "phone", 0x0A, json.dumps({"t": "media", "avail": True, "playing": True, "title": "a", "artist": "b",
                                             "pos_s": 0, "dur_s": 0, "pad": "x" * 2100}), "too_long"),
]


def binary_valid() -> list[tuple[str, str, dict]]:
    return [
        ("pose", "pose", {"epoch": 7, "t_ms": 123456, "x_dm": -15234, "y_dm": 98765, "heading_cdeg": 27050,
                          "speed_cms": 1389, "acc_m": 5}),
        ("pose-unknown-accuracy", "pose", {"epoch": 0, "t_ms": 4294967295, "x_dm": 2147483647, "y_dm": -2147483648,
                                           "heading_cdeg": 0, "speed_cms": 0, "acc_m": 255}),
        ("route", "route", {"route_id": 3, "version": 2, "backend": 0, "epoch": 7, "mode": 0, "quantum_dm": 10,
                            "points": [[0, 0], [120, 5], [240, 300], [-500, 1200], [-520, 1250]],
                            "markers": [[2, 4, 0], [4, 11, 0]]}),
        ("route-two-points", "route", {"route_id": 9, "version": 0, "backend": 3, "epoch": 1, "mode": 1, "quantum_dm": 10,
                                       "points": [[100000, -100000], [100010, -99990]], "markers": []}),
        ("traffic", "traffic", {"route_id": 3, "version": 2, "entries": [[0, 800, 0], [800, 350, 1], [1150, 120, 2], [1270, 4000, 0]]}),
        ("traffic-empty", "traffic", {"route_id": 3, "version": 2, "entries": []}),
        ("chunk-small", "chunk", {"level": 1, "tile_x": 2888, "tile_y": 1833, "data_version": 20261018, "provenance": 2,
                                  "epoch": 7, "origin_x_m": 0, "origin_y_m": 0, "quantum_dm": 80, "extent": 1160,
                                  "lines": [[0, [[0, 0], [500, 40], [1160, 90]]], [2, [[300, -10], [320, 600]]]],
                                  "polys": [[0, [[10, 10], [60, 10], [40, 70]]]]}),
        ("chunk-city", "chunk", _city_chunk()),
        ("chunk-have", "chunk_have", {"epoch": 7, "entries": [[0, 11553, 7334, 20261018], [1, 2888, 1833, 20261018]]}),
        ("chunk-have-empty", "chunk_have", {"epoch": 0, "entries": []}),
    ]


def binary_invalid() -> list[tuple[str, str, str, str]]:
    pose = encode_binary("pose", binary_valid()[0][2])
    route = encode_binary("route", binary_valid()[2][2])
    chunk = encode_binary("chunk", binary_valid()[6][2])
    one_point = bytearray(struct.pack("<BIHBHBB", 1, 1, 0, 0, 7, 0, 10)) + varint(1) + _points([[0, 0]]) + varint(0)
    bad_prov = bytearray(chunk)
    bad_prov[14] = 0  # provenance byte
    overlap = encode_binary("traffic", {"route_id": 3, "version": 2, "entries": [[0, 800, 0]]}) \
        + b""  # rebuilt below with an overlap
    overlap = bytearray(struct.pack("<BIH", 1, 3, 2)) + varint(2) + varint(0) + varint(800) + b"\x00" + varint(500) + varint(10) + b"\x01"
    many = bytearray(struct.pack("<BH", 1, 7)) + varint(513)
    return [
        ("pose-short", "pose", pose[:-1].hex(), "invalid"),
        ("pose-trailing", "pose", (pose + b"\x00").hex(), "invalid"),
        ("pose-heading", "pose", (pose[:15] + struct.pack("<H", 36000) + pose[17:]).hex(), "invalid"),
        ("version-2", "pose", (b"\x02" + pose[1:]).hex(), "ver_unknown"),
        ("route-one-point", "route", bytes(one_point).hex(), "invalid"),
        ("route-truncated", "route", route[:-3].hex(), "invalid"),
        ("varint-overflow", "route", (route[:13] + b"\xff\xff\xff\xff\x7f").hex(), "invalid"),
        ("route-out-of-range", "route", (bytearray(struct.pack("<BIHBHBB", 1, 1, 0, 0, 7, 0, 10)) + varint(2)
                                         + _points([[ROUTE_LIMIT - 1, 0], [ROUTE_LIMIT, 0]]) + varint(0)).hex(), "invalid"),
        ("chunk-provenance-0", "chunk", bytes(bad_prov).hex(), "invalid"),
        ("traffic-overlap", "traffic", bytes(overlap).hex(), "invalid"),
        ("chunk-have-too-many", "chunk_have", bytes(many).hex(), "invalid"),
    ]


def framing_vectors() -> dict:
    nav = _json_bytes(JSON_VALID[11][2])
    city = encode_binary("chunk", _city_chunk())
    route = encode_binary("route", binary_valid()[2][2])
    send = []
    for name, t, mid, payload, mtu in (("single-frame-nav", 0x07, 5, nav, 247),
                                       ("chunk-city-mtu-247", 0x43, 6, city, 247),
                                       ("chunk-city-mtu-517", 0x43, 7, city, 517),
                                       ("route-mtu-247", 0x41, 8, route, 247),
                                       ("empty-payload", 0x07, 9, b"", 247)):
        send.append({"name": name, "type": t, "msg_id": mid, "mtu": mtu, "payload": payload.hex(),
                     "frames": [f.hex() for f in fragment(t, mid, payload, mtu)]})

    # Receiving: sequences of frames and the expected events.
    big = fragment(0x43, 6, city, 247)
    nav_frame = fragment(0x07, 10, nav, 247)[0]
    bad_crc = bytearray(big[0])
    bad_crc[5] ^= 0xFF
    other_id = bytearray(big[2])
    other_id[1] = 99
    short_total = bytearray(big[0])
    struct.pack_into("<H", short_total, 3, len(city) - 1)
    long_total = bytearray(big[0])
    struct.pack_into("<H", long_total, 3, len(city) + 1)
    long_route = encode_binary("route", {"route_id": 4, "version": 0, "backend": 2, "epoch": 7, "mode": 0,
                                         "quantum_dm": 10, "points": [[k * 37, (k * k) % 900] for k in range(300)],
                                         "markers": []})
    huge = bytearray(big[0])
    struct.pack_into("<H", huge, 3, MAX_MESSAGE + 1)
    receive = [
        ("whole-chunk", [*big]),
        ("nav-between-fragments", [big[0], big[1], nav_frame, *big[2:]]),
        ("bad-crc", [bytes(bad_crc), *big[1:]]),
        ("stray-continuation", [big[3]]),
        ("out-of-order", [big[0], big[2]]),
        ("mismatched-msg-id", [big[0], big[1], bytes(other_id)]),
        ("cut-short-by-new-first", [big[0], big[1], *fragment(0x41, 8, long_route, 247)]),
        ("total-len-short", [bytes(short_total), *big[1:]]),
        ("total-len-long", [bytes(long_total), *big[1:]]),
        ("total-too-long", [bytes(huge)]),
        ("short-frame", [b"\x07\x01"]),
    ]
    recv = []
    for name, frames in receive:
        r = Reassembler()
        events = []
        for f in frames:
            for e in r.feed(bytes(f)):
                if e[0] == "message":
                    events.append({"message": {"type": e[1], "msg_id": e[2], "payload": e[3].hex()}})
                else:
                    events.append({"drop": e[1]})
        recv.append({"name": name, "frames": [bytes(f).hex() for f in frames], "events": events})
    return {"send": send, "receive": recv}


def make_vectors(out: Path) -> dict:
    vectors = {
        "crc16.json": {"algorithm": "CRC-16/CCITT-FALSE", "cases": [
            {"input": s.encode().hex(), "crc": f"0x{crc16(s.encode()):04X}"} for s in ("123456789", "", "sārathi")]},
        "framing.json": framing_vectors(),
        "json_messages.json": {
            "valid": [{"name": n, "direction": d, "type": CODES[o["t"]], "payload": _json_bytes(o).decode(),
                       "canonical": canonical_json(o["t"], d, o).decode()} for n, d, o in JSON_VALID],
            "invalid": [{"name": n, "direction": d, "type": t, "payload": p, "error": e} for n, d, t, p, e in JSON_INVALID]},
        "binary_messages.json": {
            "valid": [{"name": n, "type": CODES[t], "hex": encode_binary(t, m).hex(), "decoded": m} for n, t, m in binary_valid()],
            "invalid": [{"name": n, "type": CODES[t], "hex": h, "error": e} for n, t, h, e in binary_invalid()]},
    }
    out.mkdir(parents=True, exist_ok=True)
    for name, content in vectors.items():
        (out / name).write_text(json.dumps(content, indent=1, ensure_ascii=False) + "\n")
    return vectors


def check_vectors(directory: Path) -> int:
    failures = 0

    def fail(msg: str) -> None:
        nonlocal failures
        failures += 1
        print(f"FAIL {msg}")

    crc = json.loads((directory / "crc16.json").read_text())
    for c in crc["cases"]:
        if f"0x{crc16(bytes.fromhex(c['input'])):04X}" != c["crc"]:
            fail(f"crc {c}")
    if crc16(b"123456789") != 0x29B1:
        fail("crc check value")

    framing = json.loads((directory / "framing.json").read_text())
    for v in framing["send"]:
        frames = [f.hex() for f in fragment(v["type"], v["msg_id"], bytes.fromhex(v["payload"]), v["mtu"])]
        if frames != v["frames"]:
            fail(f"framing send {v['name']}")
        if any(len(bytes.fromhex(f)) > v["mtu"] - 3 for f in v["frames"]):
            fail(f"framing send {v['name']}: frame larger than MTU allows")
    for v in framing["receive"]:
        r = Reassembler()
        events = []
        for f in v["frames"]:
            for e in r.feed(bytes.fromhex(f)):
                events.append({"message": {"type": e[1], "msg_id": e[2], "payload": e[3].hex()}} if e[0] == "message"
                              else {"drop": e[1]})
        if events != v["events"]:
            fail(f"framing receive {v['name']}: {events} != {v['events']}")

    js = json.loads((directory / "json_messages.json").read_text())
    for v in js["valid"]:
        try:
            validate_json(v["type"], v["payload"].encode(), v["direction"])
        except Invalid as e:
            fail(f"json valid {v['name']}: {e}")
            continue
        obj = json.loads(v["payload"])
        if canonical_json(obj["t"], v["direction"], obj).decode() != v["canonical"]:
            fail(f"json canonical {v['name']}")
    for v in js["invalid"]:
        try:
            validate_json(v["type"], v["payload"].encode(), v["direction"])
            fail(f"json invalid {v['name']} was accepted")
        except Invalid as e:
            if not str(e).startswith(v["error"]):
                fail(f"json invalid {v['name']}: {e} != {v['error']}")

    bn = json.loads((directory / "binary_messages.json").read_text())
    for v in bn["valid"]:
        name = TYPES[v["type"]]
        if encode_binary(name, v["decoded"]).hex() != v["hex"]:
            fail(f"binary encode {v['name']}")
        try:
            if decode_binary(name, bytes.fromhex(v["hex"])) != v["decoded"]:
                fail(f"binary decode {v['name']}")
        except Invalid as e:
            fail(f"binary valid {v['name']}: {e}")
        if len(bytes.fromhex(v["hex"])) > MAX_MESSAGE:
            fail(f"binary {v['name']} over the message limit")
    for v in bn["invalid"]:
        try:
            decode_binary(TYPES[v["type"]], bytes.fromhex(v["hex"]))
            fail(f"binary invalid {v['name']} was accepted")
        except Invalid as e:
            if not str(e).startswith(v["error"]):
                fail(f"binary invalid {v['name']}: {e} != {v['error']}")

    count = (len(crc["cases"]) + len(framing["send"]) + len(framing["receive"]) + len(js["valid"])
             + len(js["invalid"]) + len(bn["valid"]) + len(bn["invalid"]))
    print(f"{count} vectors, {failures} failures")
    return 1 if failures else 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = parser.add_subparsers(dest="command", required=True)
    sub.add_parser("make-vectors").add_argument("dir", type=Path)
    sub.add_parser("check-vectors").add_argument("dir", type=Path)
    args = parser.parse_args()
    if args.command == "make-vectors":
        make_vectors(args.dir)
        return check_vectors(args.dir)
    return check_vectors(args.dir)


if __name__ == "__main__":
    sys.exit(main())
