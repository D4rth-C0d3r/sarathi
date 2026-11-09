#!/usr/bin/env python3
"""Map chunks for the on-device map (ADR-0009, protocol.md section 4 `chunk`).

The reference for the phone's map pipeline (Phase 4 step G2): the app's Kotlin pipeline must
produce the same bytes from the same tiles. Order of work, as on the phone:

1. Decode the vector tile (OpenMapTiles schema, as served by OpenFreeMap).
2. Keep the level's road classes (and water and parks), in tile units.
3. Clip to the tile, merge connected roads of one class, simplify (Douglas-Peucker) with the
   level's tolerance converted to tile units. This is what the phone caches: it doesn't depend
   on a ride.
4. Project into a ride frame (the app's RideFrame), quantize relative to the chunk's origin
   (the tile's south-west corner), drop repeated points, encode.

Commands:
  filter IN OUT                      keep only the layers chunks use (fixtures stay small)
  extract REGION.pmtiles OUT Z/X/Y...   offline tiles from a PMTiles region, filtered
  --schema protomaps (chunks)        Protomaps basemap tiles (offline); default OpenMapTiles
  chunks --frame LAT,LON --epoch N --out DIR TILE...
                                     one chunk per tile, TILE as path to z-x-y.pbf
  measure TILE...                    sizes per level, like ADR-0009's table

Standard library only; run from the project .venv.
"""

from __future__ import annotations

import argparse
import gzip
import math
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from protocol_vectors import MAX_MESSAGE, encode_binary  # noqa: E402

# ---- Levels (ADR-0009 "Chunks") ----

ROAD_CLASSES = ["motorway", "trunk", "primary", "secondary", "tertiary", "minor", "service"]
LAYERS = {"transportation", "water", "park"}
WATER, PARK = 0, 1


class Schema:
    """How a source's tiles map to chunk roads and areas (classes 0-6; water and parks)."""

    def __init__(self, name: str, provenance: int, layers: set[str], road_layer: str) -> None:
        self.name, self.provenance, self.layers, self.road_layer = name, provenance, layers, road_layer

    def road_class(self, tags: dict) -> int | None:
        raise NotImplementedError

    def areas(self, tile: dict):
        """(kind, feature) for the polygons to keep, in a fixed order: water, then parks."""
        raise NotImplementedError


class OpenMapTiles(Schema):
    """OpenFreeMap (online): transportation by class; water; park."""

    def __init__(self) -> None:
        super().__init__("openmaptiles", 1, LAYERS, "transportation")

    def road_class(self, tags: dict) -> int | None:
        cls = tags.get("class")
        return ROAD_CLASSES.index(cls) if cls in ROAD_CLASSES else None

    def areas(self, tile: dict):
        for f in tile.get("water", []):
            yield WATER, f
        for f in tile.get("park", []):
            yield PARK, f


# Protomaps basemap v4 (the offline region): roads by kind_detail (the OSM highway tag).
PROTOMAPS_ROADS = {
    "motorway": 0, "motorway_link": 0, "trunk": 1, "trunk_link": 1, "primary": 2,
    "primary_link": 2, "secondary": 3, "secondary_link": 3, "tertiary": 4, "tertiary_link": 4,
    "residential": 5, "unclassified": 5, "living_street": 5, "road": 5, "service": 6,
}
PROTOMAPS_PARKS = {"park", "national_park", "nature_reserve", "protected_area"}
PROTOMAPS_NOT_WATER = {"swimming_pool"}


class Protomaps(Schema):
    """Protomaps basemap v4 (offline, PMTiles): roads, water, landuse parks."""

    def __init__(self) -> None:
        super().__init__("protomaps", 2, {"roads", "water", "landuse"}, "roads")

    def road_class(self, tags: dict) -> int | None:
        return PROTOMAPS_ROADS.get(tags.get("kind_detail"))

    def areas(self, tile: dict):
        for f in tile.get("water", []):
            if f["tags"].get("kind") not in PROTOMAPS_NOT_WATER:
                yield WATER, f
        for f in tile.get("landuse", []):
            if f["tags"].get("kind") in PROTOMAPS_PARKS:
                yield PARK, f


SCHEMAS = {"openmaptiles": OpenMapTiles(), "protomaps": Protomaps()}


class Level:
    def __init__(self, level: int, zoom: int, max_class: int, quantum_dm: int, polys: bool,
                 min_water_m2: float) -> None:
        self.level, self.zoom, self.max_class = level, zoom, max_class
        self.quantum_dm, self.polys, self.min_water_m2 = quantum_dm, polys, min_water_m2

    @property
    def quantum_m(self) -> float:
        return self.quantum_dm / 10


# L0: z14, every class to service, water and parks, 2 m. L1: z12, motorway to secondary, only
# large water, 8 m. Tolerance equals the quantum.
LEVELS = {14: Level(0, 14, 6, 20, True, 0.0), 12: Level(1, 12, 3, 80, True, 40_000.0)}
MAX_LINE_POINTS = 1024
MAX_CHUNK_POINTS = 8192
MIN_POLY_QUANTA2 = 16  # smaller polygons (after quantizing) aren't worth drawing

# ---- Protocol buffers (just what vector tiles need) ----


def _varint(data: bytes, i: int) -> tuple[int, int]:
    shift = result = 0
    while True:
        b = data[i]
        i += 1
        result |= (b & 0x7F) << shift
        if not b & 0x80:
            return result, i
        shift += 7


def fields(data: bytes):
    """Yields (field number, wire type, value, raw bytes of the whole field)."""
    i = 0
    while i < len(data):
        start = i
        key, i = _varint(data, i)
        number, wire = key >> 3, key & 7
        if wire == 0:
            value, i = _varint(data, i)
        elif wire == 1:
            value = data[i:i + 8]
            i += 8
        elif wire == 2:
            length, i = _varint(data, i)
            value = data[i:i + length]
            i += length
        elif wire == 5:
            value = data[i:i + 4]
            i += 4
        else:
            raise ValueError(f"wire type {wire}")
        yield number, wire, value, data[start:i]


def packed(data: bytes) -> list[int]:
    out, i = [], 0
    while i < len(data):
        v, i = _varint(data, i)
        out.append(v)
    return out


def _value(data: bytes):
    for number, _, v, _ in fields(data):
        if number == 1:
            return v.decode()
        if number == 2:
            return struct.unpack("<f", v)[0]
        if number == 3:
            return struct.unpack("<d", v)[0]
        if number in (4, 5):
            return v
        if number == 6:
            return (v >> 1) ^ -(v & 1)
        if number == 7:
            return bool(v)
    return None


def read_tile(data: bytes, layers: set[str] = LAYERS) -> dict[str, list[dict]]:
    """Features of the layers chunks use: {layer: [{"type", "tags", "rings"}]}, rings in tile
    units scaled to 4096 whatever the layer's extent."""
    out: dict[str, list[dict]] = {}
    for number, _, layer, _ in fields(data):
        if number != 3:
            continue
        name, feats, keys, values, extent = "", [], [], [], 4096
        for n, _, v, _ in fields(layer):
            if n == 1:
                name = v.decode()
            elif n == 2:
                feats.append(v)
            elif n == 3:
                keys.append(v.decode())
            elif n == 4:
                values.append(_value(v))
            elif n == 5:
                extent = v
        if name not in layers:
            continue
        scale = 4096 / extent
        decoded = []
        for f in feats:
            tags, gtype, geometry = [], 0, []
            for n, _, v, _ in fields(f):
                if n == 2:
                    tags = packed(v)
                elif n == 3:
                    gtype = v
                elif n == 4:
                    geometry = packed(v)
            props = {keys[tags[k]]: values[tags[k + 1]] for k in range(0, len(tags), 2)}
            decoded.append({"type": gtype, "tags": props, "rings": _geometry(geometry, scale)})
        out[name] = decoded
    return out


def _geometry(cmds: list[int], scale: float) -> list[list[tuple[float, float]]]:
    rings: list[list[tuple[float, float]]] = []
    x = y = 0
    i = 0
    while i < len(cmds):
        cmd, count = cmds[i] & 7, cmds[i] >> 3
        i += 1
        if cmd == 7:  # ClosePath
            continue
        for _ in range(count):
            dx, dy = cmds[i], cmds[i + 1]
            i += 2
            x += (dx >> 1) ^ -(dx & 1)
            y += (dy >> 1) ^ -(dy & 1)
            if cmd == 1:
                rings.append([])
            rings[-1].append((x * scale, y * scale))
    return rings


def filter_tile(data: bytes, layers: set[str] = LAYERS) -> bytes:
    """The same tile with only the layers chunks use, copied byte for byte."""
    out = bytearray()
    for number, _, layer, raw in fields(data):
        if number != 3:
            continue
        name = next(v.decode() for n, _, v, _ in fields(layer) if n == 1)
        if name in layers:
            out += raw
    return bytes(out)


# ---- PMTiles v3 (the offline region, ADR-0009) ----


class PMTiles:
    """Reads tiles from a PMTiles v3 archive: Hilbert tile IDs, gzipped directories."""

    def __init__(self, data: bytes) -> None:
        if data[:7] != b"PMTiles" or data[7] != 3:
            raise ValueError("not a PMTiles v3 archive")
        (self.root_off, self.root_len, _, _, self.leaf_off, _, self.tile_off, _, _, _, _) = \
            struct.unpack_from("<11Q", data, 8)
        _, self.icomp, self.tcomp, self.ttype, self.minz, self.maxz = struct.unpack_from("<6B", data, 96)
        self.data = data

    def _decompress(self, blob: bytes, comp: int) -> bytes:
        if comp == 2:
            return gzip.decompress(blob)
        if comp == 1:
            return blob
        raise ValueError(f"compression {comp}")

    def _directory(self, off: int, length: int) -> list[tuple[int, int, int, int]]:
        d = self._decompress(self.data[off:off + length], self.icomp)
        i = 0
        n, i = _varint(d, i)
        ids, runs, lens, offs = [], [], [], []
        last = 0
        for _ in range(n):
            v, i = _varint(d, i)
            last += v
            ids.append(last)
        for _ in range(n):
            v, i = _varint(d, i)
            runs.append(v)
        for _ in range(n):
            v, i = _varint(d, i)
            lens.append(v)
        for k in range(n):
            v, i = _varint(d, i)
            offs.append(offs[k - 1] + lens[k - 1] if v == 0 and k > 0 else v - 1)
        return list(zip(ids, runs, lens, offs))

    def tile(self, z: int, x: int, y: int) -> bytes | None:
        tid = tile_id(z, x, y)
        off, length = self.root_off, self.root_len
        for _ in range(4):  # root and up to three levels of leaves
            entries = self._directory(off, length)
            found = None
            for e in entries:
                if e[0] > tid:
                    break
                found = e
            if found is None:
                return None
            eid, run, elen, eoff = found
            if run == 0:  # a leaf directory
                off, length = self.leaf_off + eoff, elen
                continue
            if tid >= eid + run:
                return None
            return self._decompress(self.data[self.tile_off + eoff:self.tile_off + eoff + elen], self.tcomp)
        return None


def tile_id(z: int, x: int, y: int) -> int:
    """PMTiles' Hilbert tile ID."""
    acc = sum(4 ** k for k in range(z))  # tiles in all lower zooms
    n = 1 << z
    d = 0
    s = n >> 1
    tx, ty = x, y
    while s > 0:
        rx = 1 if tx & s else 0
        ry = 1 if ty & s else 0
        d += s * s * ((3 * rx) ^ ry)
        if ry == 0:
            if rx == 1:
                tx = s - 1 - tx
                ty = s - 1 - ty
            tx, ty = ty, tx
        s >>= 1
    return acc + d


# ---- Geometry in tile units (0..4096, y down) ----

EXTENT = 4096.0


def clip_line(points: list[tuple[float, float]]) -> list[list[tuple[float, float]]]:
    """Parts of a polyline inside the tile (Liang-Barsky per segment)."""
    parts: list[list[tuple[float, float]]] = []
    current: list[tuple[float, float]] = []
    for (x0, y0), (x1, y1) in zip(points, points[1:]):
        t0, t1 = 0.0, 1.0
        dx, dy = x1 - x0, y1 - y0
        inside = True
        for p, q in ((-dx, x0), (dx, EXTENT - x0), (-dy, y0), (dy, EXTENT - y0)):
            if p == 0:
                if q < 0:
                    inside = False
                    break
                continue
            r = q / p
            if p < 0:
                t0 = max(t0, r)
            else:
                t1 = min(t1, r)
            if t0 > t1:
                inside = False
                break
        if not inside:
            if len(current) >= 2:
                parts.append(current)
            current = []
            continue
        a = (x0 + t0 * dx, y0 + t0 * dy)
        b = (x0 + t1 * dx, y0 + t1 * dy)
        if not current or current[-1] != a:
            if len(current) >= 2:
                parts.append(current)
            current = [a]
        current.append(b)
        if t1 < 1.0:
            if len(current) >= 2:
                parts.append(current)
            current = []
    if len(current) >= 2:
        parts.append(current)
    return parts


def clip_polygon(ring: list[tuple[float, float]]) -> list[tuple[float, float]]:
    """Sutherland-Hodgman against the tile's four edges."""
    def edge(pts, inside, cross):
        out = []
        for i, cur in enumerate(pts):
            prev = pts[i - 1]
            if inside(cur):
                if not inside(prev):
                    out.append(cross(prev, cur))
                out.append(cur)
            elif inside(prev):
                out.append(cross(prev, cur))
        return out

    def at_x(xe):
        return lambda a, b: (xe, a[1] + (b[1] - a[1]) * (xe - a[0]) / (b[0] - a[0]))

    def at_y(ye):
        return lambda a, b: (a[0] + (b[0] - a[0]) * (ye - a[1]) / (b[1] - a[1]), ye)

    pts = ring
    for inside, cross in ((lambda p: p[0] >= 0, at_x(0.0)), (lambda p: p[0] <= EXTENT, at_x(EXTENT)),
                          (lambda p: p[1] >= 0, at_y(0.0)), (lambda p: p[1] <= EXTENT, at_y(EXTENT))):
        if not pts:
            break
        pts = edge(pts, inside, cross)
    return pts


def area2(ring: list[tuple[float, float]]) -> float:
    """Twice the signed area (positive for a vector tile's outer ring, y down)."""
    return sum(a[0] * b[1] - b[0] * a[1] for a, b in zip(ring, ring[1:] + ring[:1]))


def merge_lines(lines: list[list[tuple[float, float]]]) -> list[list[tuple[float, float]]]:
    """Joins lines of one class where exactly two line ends meet, in input order."""
    lines = [list(l) for l in lines]
    ends: dict[tuple[float, float], list[int]] = {}
    for i, l in enumerate(lines):
        ends.setdefault(l[0], []).append(i)
        ends.setdefault(l[-1], []).append(i)
    alive = [True] * len(lines)
    for i in range(len(lines)):
        if not alive[i]:
            continue
        for at_end in (True, False):
            while True:
                line = lines[i]
                p = line[-1] if at_end else line[0]
                others = [j for j in ends.get(p, []) if j != i and alive[j]]
                if len(ends.get(p, [])) != 2 or len(others) != 1 or line[0] == line[-1]:
                    break
                j = others[0]
                other = lines[j]
                if other[0] == other[-1]:
                    break
                if at_end:
                    seg = other if other[0] == p else other[::-1]
                    lines[i] = line + seg[1:]
                else:
                    seg = other if other[-1] == p else other[::-1]
                    lines[i] = seg[:-1] + line
                alive[j] = False
                far = seg[-1] if at_end else seg[0]
                ends[far] = [i if k == j else k for k in ends[far]]
                ends[p] = []
    return [l for i, l in enumerate(lines) if alive[i]]


def simplify(points: list[tuple[float, float]], tol: float) -> list[tuple[float, float]]:
    """Douglas-Peucker, iterative, keeping both ends."""
    if len(points) < 3:
        return points
    keep = [False] * len(points)
    keep[0] = keep[-1] = True
    stack = [(0, len(points) - 1)]
    tol2 = tol * tol
    while stack:
        a, b = stack.pop()
        ax, ay = points[a]
        bx, by = points[b]
        dx, dy = bx - ax, by - ay
        len2 = dx * dx + dy * dy
        best, index = -1.0, -1
        for k in range(a + 1, b):
            px, py = points[k]
            if len2 == 0:
                d2 = (px - ax) ** 2 + (py - ay) ** 2
            else:
                t = max(0.0, min(1.0, ((px - ax) * dx + (py - ay) * dy) / len2))
                d2 = (px - ax - t * dx) ** 2 + (py - ay - t * dy) ** 2
            if d2 > best:
                best, index = d2, k
        if best > tol2:
            keep[index] = True
            stack.append((a, index))
            stack.append((index, b))
    return [p for p, k in zip(points, keep) if k]


# ---- Ride frame (the app's RideFrame, core:protocol) ----

A = 6_378_137.0
E2 = 6.694379990141317e-3
DEG = math.pi / 180


class RideFrame:
    def __init__(self, lat: float, lon: float) -> None:
        self.lat0 = round_half_up(lat * 100) / 100
        self.lon0 = round_half_up(lon * 100) / 100
        phi = self.lat0 * DEG
        s = math.sin(phi)
        w = math.sqrt(1 - E2 * s * s)
        self.m_lat = A * (1 - E2) / (w * w * w)
        self.m_lon = A / w * math.cos(phi)

    def project(self, lat: float, lon: float) -> tuple[float, float]:
        d_lon = lon - self.lon0
        if d_lon > 180:
            d_lon -= 360
        if d_lon < -180:
            d_lon += 360
        return d_lon * DEG * self.m_lon, (lat - self.lat0) * DEG * self.m_lat


def round_half_up(v: float) -> int:
    return math.floor(v + 0.5)


def tile_to_lat_lon(z: int, tx: int, ty: int, u: float, v: float) -> tuple[float, float]:
    n = 1 << z
    lon = (tx + u / EXTENT) / n * 360 - 180
    lat = math.degrees(math.atan(math.sinh(math.pi * (1 - 2 * (ty + v / EXTENT) / n))))
    return lat, lon


def metres_per_unit(z: int, ty: int) -> float:
    """Ground metres per tile unit at the tile's centre latitude (Web Mercator)."""
    lat, _ = tile_to_lat_lon(z, 0, ty, 0, EXTENT / 2)
    return 2 * math.pi * A * math.cos(lat * DEG) / ((1 << z) * EXTENT)


# ---- Tile -> chunk ----

def tile_shapes(tile: dict, level: Level, ty: int, schema: Schema) -> tuple[list, list]:
    """Step 3: what the phone caches. Lines [(class, points)], polygons [(kind, ring)], tile units."""
    tol = level.quantum_m / metres_per_unit(level.zoom, ty)
    by_class: dict[int, list] = {}
    for f in tile.get(schema.road_layer, []):
        c = schema.road_class(f["tags"])
        if f["type"] != 2 or c is None or c > level.max_class:
            continue
        for ring in f["rings"]:
            by_class.setdefault(c, []).extend(clip_line(ring))
    lines = []
    for c in sorted(by_class):
        for l in merge_lines(by_class[c]):
            s = simplify(l, tol)
            if len(s) >= 2:
                lines.append((c, s))
    polys = []
    if level.polys:
        unit2 = metres_per_unit(level.zoom, ty) ** 2
        for kind, f in schema.areas(tile):
            if kind == PARK and level.level == 1:
                continue
            if f["type"] != 3:
                continue
            for ring in f["rings"]:
                if area2(ring) <= 0:
                    continue  # holes and degenerate rings: one outer ring per polygon
                clipped = clip_polygon(ring)
                if len(clipped) < 3 or area2(clipped) / 2 * unit2 < level.min_water_m2:
                    continue
                s = simplify(clipped + [clipped[0]], tol)[:-1]
                if len(s) >= 3:
                    polys.append((kind, s))
    return lines, polys


def make_chunk(path: Path, frame: RideFrame, epoch: int, data_version: int,
               schema: Schema) -> dict:
    z, tx, ty = (int(p) for p in path.stem.split("-"))
    level = LEVELS[z]
    lines, polys = tile_shapes(read_tile(path.read_bytes(), schema.layers), level, ty, schema)
    q = level.quantum_m
    sw = frame.project(*tile_to_lat_lon(z, tx, ty, 0, EXTENT))
    ne = frame.project(*tile_to_lat_lon(z, tx, ty, EXTENT, 0))
    ox, oy = round_half_up(sw[0]), round_half_up(sw[1])
    extent = math.ceil(max(ne[0] - sw[0], ne[1] - sw[1]) / q)

    def quantize(pts):
        out = []
        for u, v in pts:
            x, y = frame.project(*tile_to_lat_lon(z, tx, ty, u, v))
            p = [round_half_up((x - ox) / q), round_half_up((y - oy) / q)]
            if not out or out[-1] != p:
                out.append(p)
        return out

    out_lines = []
    for c, pts in lines:
        qp = quantize(pts)
        for start in range(0, len(qp) - 1, MAX_LINE_POINTS - 1):
            piece = qp[start:start + MAX_LINE_POINTS]
            if len(piece) >= 2:
                out_lines.append([c, piece])
    out_polys = []
    for kind, ring in polys:
        qr = quantize(ring)
        if len(qr) > 1 and qr[0] == qr[-1]:
            qr = qr[:-1]
        if len(qr) >= 3 and abs(area2([tuple(p) for p in qr])) / 2 >= MIN_POLY_QUANTA2:
            out_polys.append([kind, qr[:MAX_LINE_POINTS]])
    return {"level": level.level, "tile_x": tx, "tile_y": ty, "data_version": data_version,
            "provenance": schema.provenance, "epoch": epoch, "origin_x_m": ox, "origin_y_m": oy,
            "quantum_dm": level.quantum_dm, "extent": extent, "lines": out_lines,
            "polys": out_polys}


def summary(m: dict, data: bytes) -> str:
    points = sum(len(p) for _, p in m["lines"]) + sum(len(p) for _, p in m["polys"])
    limit = "" if len(data) <= MAX_MESSAGE and points <= MAX_CHUNK_POINTS else "  OVER THE LIMIT"
    return (f"L{m['level']} {m['tile_x']}/{m['tile_y']}: {len(m['lines'])} lines, "
            f"{len(m['polys'])} polygons, {points} points, {len(data) / 1024:.1f} KB{limit}")


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    f = sub.add_parser("filter")
    f.add_argument("src", type=Path)
    f.add_argument("dst", type=Path)
    e = sub.add_parser("extract", help="tiles from a PMTiles region, filtered (fixtures)")
    e.add_argument("region", type=Path)
    e.add_argument("out", type=Path)
    e.add_argument("tiles", nargs="+", help="Z/X/Y")
    c = sub.add_parser("chunks")
    c.add_argument("--schema", choices=sorted(SCHEMAS), default="openmaptiles")
    c.add_argument("--frame", required=True, help="LAT,LON near the ride's start")
    c.add_argument("--epoch", type=int, default=1)
    c.add_argument("--data-version", type=int, required=True)
    c.add_argument("--out", type=Path, required=True)
    c.add_argument("tiles", type=Path, nargs="+")
    args = ap.parse_args()
    if args.cmd == "filter":
        args.dst.write_bytes(filter_tile(args.src.read_bytes()))
        return 0
    if args.cmd == "extract":
        region = PMTiles(args.region.read_bytes())
        args.out.mkdir(parents=True, exist_ok=True)
        for zxy in args.tiles:
            z, x, y = (int(v) for v in zxy.split("/"))
            data = region.tile(z, x, y)
            if data is None:
                print(f"{zxy}: not in the region")
                continue
            (args.out / f"{z}-{x}-{y}.pbf").write_bytes(filter_tile(data, SCHEMAS["protomaps"].layers))
        return 0
    lat, lon = (float(v) for v in args.frame.split(","))
    frame = RideFrame(lat, lon)
    args.out.mkdir(parents=True, exist_ok=True)
    over = False
    for path in sorted(args.tiles):
        m = make_chunk(path, frame, args.epoch, args.data_version, SCHEMAS[args.schema])
        data = encode_binary("chunk", m)
        (args.out / f"{path.stem}.bin").write_bytes(data)
        line = summary(m, data)
        over |= "OVER" in line
        print(line)
    return 1 if over else 0


if __name__ == "__main__":
    sys.exit(main())
