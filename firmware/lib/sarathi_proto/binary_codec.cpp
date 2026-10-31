#include "binary_codec.h"

// Messages arrive and leave as a pointer and a length.
// NOLINTBEGIN(cppcoreguidelines-pro-bounds-pointer-arithmetic)

namespace sarathi::proto {
namespace {

constexpr uint64_t kVarintLimit = 1ULL << 32U;

class Reader {
 public:
  Reader(const uint8_t* data, size_t size) : data_(data), size_(size) {}

  bool U8(uint8_t* v) {
    if (size_ - pos_ < 1) {
      return false;
    }
    *v = data_[pos_++];
    return true;
  }

  bool U16(uint16_t* v) {
    uint64_t x = 0;
    if (!Fixed(2, &x)) {
      return false;
    }
    *v = static_cast<uint16_t>(x);
    return true;
  }

  bool U32(uint32_t* v) {
    uint64_t x = 0;
    if (!Fixed(4, &x)) {
      return false;
    }
    *v = static_cast<uint32_t>(x);
    return true;
  }

  bool I32(int32_t* v) {
    uint32_t x = 0;
    if (!U32(&x)) {
      return false;
    }
    *v = static_cast<int32_t>(x);
    return true;
  }

  // Unsigned LEB128, at most 5 bytes, below 2^32.
  bool Varint(uint32_t* v) {
    uint64_t value = 0;
    for (unsigned shift = 0; shift < 35; shift += 7) {
      uint8_t b = 0;
      if (!U8(&b)) {
        return false;
      }
      value |= static_cast<uint64_t>(b & 0x7FU) << shift;
      if ((b & 0x80U) == 0) {
        if (value >= kVarintLimit) {
          return false;
        }
        *v = static_cast<uint32_t>(value);
        return true;
      }
    }
    return false;
  }

  bool Zigzag(int64_t* v) {
    uint32_t u = 0;
    if (!Varint(&u)) {
      return false;
    }
    *v = static_cast<int64_t>(u >> 1U) ^ -static_cast<int64_t>(u & 1U);
    return true;
  }

  [[nodiscard]] bool Done() const { return pos_ == size_; }

 private:
  bool Fixed(size_t n, uint64_t* v) {
    if (size_ - pos_ < n) {
      return false;
    }
    uint64_t x = 0;
    for (size_t i = 0; i < n; ++i) {
      x |= static_cast<uint64_t>(data_[pos_ + i]) << (8 * i);
    }
    pos_ += n;
    *v = x;
    return true;
  }

  const uint8_t* data_;
  size_t size_;
  size_t pos_ = 0;
};

class Writer {
 public:
  Writer(uint8_t* out, size_t capacity) : out_(out), capacity_(capacity) {}

  void U8(uint8_t v) {
    if (!ok_ || size_ >= capacity_) {
      ok_ = false;
      return;
    }
    out_[size_++] = v;
  }

  void Fixed(uint64_t v, size_t n) {
    for (size_t i = 0; i < n; ++i) {
      U8(static_cast<uint8_t>(v >> (8 * i)));
    }
  }

  void Varint(uint32_t v) {
    do {
      const auto b = static_cast<uint8_t>(v & 0x7FU);
      v >>= 7U;
      U8(static_cast<uint8_t>(b | (v != 0 ? 0x80U : 0U)));
    } while (v != 0);
  }

  // Points as zigzag deltas from the previous one (the first from 0, 0).
  void Points(const Point* points, size_t n) {
    int64_t px = 0;
    int64_t py = 0;
    for (size_t i = 0; i < n; ++i) {
      Delta(points[i].x - px);
      Delta(points[i].y - py);
      px = points[i].x;
      py = points[i].y;
    }
  }

  [[nodiscard]] size_t Finish() const { return ok_ ? size_ : 0; }

 private:
  void Delta(int64_t d) {
    if (d < INT32_MIN || d > INT32_MAX) {
      ok_ = false;
      return;
    }
    const auto n = static_cast<int32_t>(d);
    Varint((static_cast<uint32_t>(n) << 1U) ^ static_cast<uint32_t>(n >> 31));
  }

  uint8_t* out_;
  size_t capacity_;
  size_t size_ = 0;
  bool ok_ = true;
};

// The version byte: absent is invalid, newer is skipped.
BinError Version(Reader& r) {
  uint8_t ver = 0;
  if (!r.U8(&ver)) {
    return BinError::kInvalid;
  }
  return ver == kBinaryVersion ? BinError::kNone : BinError::kVerUnknown;
}

// Reads n delta-coded points into out, each within [lo, hi] on both axes.
bool ReadPoints(Reader& r, size_t n, int64_t lo, int64_t hi, Point* out) {
  int64_t x = 0;
  int64_t y = 0;
  for (size_t i = 0; i < n; ++i) {
    int64_t dx = 0;
    int64_t dy = 0;
    if (!r.Zigzag(&dx) || !r.Zigzag(&dy)) {
      return false;
    }
    x += dx;
    y += dy;
    if (x < lo || x > hi || y < lo || y > hi) {
      return false;
    }
    out[i] = Point{static_cast<int32_t>(x), static_cast<int32_t>(y)};
  }
  return true;
}

// Lines or polygons of a chunk, appended to the shapes and points already read.
BinError ReadShapes(Reader& r, size_t max_shapes, uint8_t max_kind, size_t min_points,
                    int64_t extent, Buffer<ChunkShape> shapes, size_t* n_shapes,
                    Buffer<Point> points, size_t* n_points, size_t* count) {
  uint32_t n = 0;
  if (!r.Varint(&n) || n > max_shapes) {
    return BinError::kInvalid;
  }
  *count = n;
  for (uint32_t i = 0; i < n; ++i) {
    uint8_t kind = 0;
    uint32_t k = 0;
    if (!r.U8(&kind) || !r.Varint(&k) || kind > max_kind || k < min_points || k > kMaxShapePoints ||
        *n_points + k > kMaxChunkPoints) {
      return BinError::kInvalid;
    }
    if (*n_shapes >= shapes.capacity || *n_points + k > points.capacity) {
      return BinError::kNoRoom;
    }
    if (!ReadPoints(r, k, -kChunkOverscan, extent + kChunkOverscan, points.data + *n_points)) {
      return BinError::kInvalid;
    }
    shapes.data[(*n_shapes)++] =
        ChunkShape{kind, static_cast<uint16_t>(k), static_cast<uint32_t>(*n_points)};
    *n_points += k;
  }
  return BinError::kNone;
}

void WriteKey(Writer& w, const ChunkKey& k) {
  w.U8(k.level);
  w.Fixed(k.tile_x, 4);
  w.Fixed(k.tile_y, 4);
  w.Fixed(k.data_version, 4);
}

bool ShapesFit(const ChunkShape* shapes, size_t n, size_t n_points) {
  for (size_t i = 0; i < n; ++i) {
    if (static_cast<size_t>(shapes[i].first) + shapes[i].count > n_points) {
      return false;
    }
  }
  return true;
}

void WriteShapes(Writer& w, const ChunkShape* shapes, size_t n, const Point* points) {
  w.Varint(static_cast<uint32_t>(n));
  for (size_t i = 0; i < n; ++i) {
    w.U8(shapes[i].kind);
    w.Varint(shapes[i].count);
    w.Points(points + shapes[i].first, shapes[i].count);
  }
}

}  // namespace

const char* BinErrorName(BinError error) {
  switch (error) {
    case BinError::kNone:
      return "none";
    case BinError::kVerUnknown:
      return "ver_unknown";
    case BinError::kInvalid:
      return "invalid";
    case BinError::kNoRoom:
      return "no_room";
  }
  return "?";
}

BinError DecodePose(const uint8_t* data, size_t size, Pose* out) {
  Reader r(data, size);
  if (const BinError e = Version(r); e != BinError::kNone) {
    return e;
  }
  Pose p;
  if (!r.U16(&p.epoch) || !r.U32(&p.t_ms) || !r.I32(&p.x_dm) || !r.I32(&p.y_dm) ||
      !r.U16(&p.heading_cdeg) || !r.U16(&p.speed_cms) || !r.U8(&p.acc_m) || !r.Done() ||
      p.heading_cdeg > kMaxHeadingCdeg) {
    return BinError::kInvalid;
  }
  *out = p;
  return BinError::kNone;
}

BinError DecodeRoute(const uint8_t* data, size_t size, Route* out, Buffer<Point> points,
                     Buffer<RouteMarker> markers) {
  Reader r(data, size);
  if (const BinError e = Version(r); e != BinError::kNone) {
    return e;
  }
  Route m;
  uint32_t n = 0;
  if (!r.U32(&m.route_id) || !r.U16(&m.version) || !r.U8(&m.backend) || !r.U16(&m.epoch) ||
      !r.U8(&m.mode) || !r.U8(&m.quantum_dm) || m.backend > 3 || m.mode > 1 || m.quantum_dm < 1 ||
      !r.Varint(&n) || n < 2 || n > kMaxRoutePoints) {
    return BinError::kInvalid;
  }
  if (n > points.capacity) {
    return BinError::kNoRoom;
  }
  if (!ReadPoints(r, n, -kRouteCoordLimit, kRouteCoordLimit - 1, points.data)) {
    return BinError::kInvalid;
  }
  uint32_t n_markers = 0;
  if (!r.Varint(&n_markers) || n_markers > kMaxRouteMarkers) {
    return BinError::kInvalid;
  }
  if (n_markers > markers.capacity) {
    return BinError::kNoRoom;
  }
  int64_t previous = -1;
  for (uint32_t i = 0; i < n_markers; ++i) {
    uint32_t point = 0;
    RouteMarker& marker = markers.data[i];
    if (!r.Varint(&point) || !r.U8(&marker.maneuver) || !r.U8(&marker.exit) ||
        static_cast<int64_t>(point) <= previous || point >= n) {
      return BinError::kInvalid;
    }
    marker.point = static_cast<uint16_t>(point);
    previous = point;
  }
  if (!r.Done()) {
    return BinError::kInvalid;
  }
  m.points = points.data;
  m.n_points = n;
  m.markers = markers.data;
  m.n_markers = n_markers;
  *out = m;
  return BinError::kNone;
}

BinError DecodeTraffic(const uint8_t* data, size_t size, Traffic* out,
                       Buffer<TrafficEntry> entries) {
  Reader r(data, size);
  if (const BinError e = Version(r); e != BinError::kNone) {
    return e;
  }
  Traffic m;
  uint32_t n = 0;
  if (!r.U32(&m.route_id) || !r.U16(&m.version) || !r.Varint(&n) || n > kMaxTrafficEntries) {
    return BinError::kInvalid;
  }
  if (n > entries.capacity) {
    return BinError::kNoRoom;
  }
  uint64_t end = 0;
  for (uint32_t i = 0; i < n; ++i) {
    TrafficEntry& e = entries.data[i];
    if (!r.Varint(&e.offset_m) || !r.Varint(&e.length_m) || !r.U8(&e.level) || e.level > 3 ||
        e.offset_m < end || e.length_m == 0) {
      return BinError::kInvalid;
    }
    end = static_cast<uint64_t>(e.offset_m) + e.length_m;
  }
  if (!r.Done()) {
    return BinError::kInvalid;
  }
  m.entries = entries.data;
  m.n_entries = n;
  *out = m;
  return BinError::kNone;
}

BinError DecodeChunk(const uint8_t* data, size_t size, Chunk* out, Buffer<ChunkShape> shapes,
                     Buffer<Point> points) {
  Reader r(data, size);
  if (const BinError e = Version(r); e != BinError::kNone) {
    return e;
  }
  Chunk m;
  if (!r.U8(&m.key.level) || !r.U32(&m.key.tile_x) || !r.U32(&m.key.tile_y) ||
      !r.U32(&m.key.data_version) || !r.U8(&m.provenance) || !r.U16(&m.epoch) ||
      !r.I32(&m.origin_x_m) || !r.I32(&m.origin_y_m) || !r.U8(&m.quantum_dm) || !r.U16(&m.extent) ||
      m.key.level > 1 || (m.provenance != 1 && m.provenance != 2) || m.quantum_dm < 1 ||
      m.extent < 1) {
    return BinError::kInvalid;
  }
  size_t n_shapes = 0;
  size_t n_points = 0;
  if (const BinError e = ReadShapes(r, kMaxChunkLines, 6, 2, m.extent, shapes, &n_shapes, points,
                                    &n_points, &m.n_lines);
      e != BinError::kNone) {
    return e;
  }
  if (const BinError e = ReadShapes(r, kMaxChunkPolys, 1, 3, m.extent, shapes, &n_shapes, points,
                                    &n_points, &m.n_polys);
      e != BinError::kNone) {
    return e;
  }
  if (!r.Done()) {
    return BinError::kInvalid;
  }
  m.lines = shapes.data;
  m.polys = shapes.data + m.n_lines;
  m.points = points.data;
  m.n_points = n_points;
  *out = m;
  return BinError::kNone;
}

size_t EncodeBinary(const Pose& m, uint8_t* out, size_t capacity) {
  Writer w(out, capacity);
  w.U8(kBinaryVersion);
  w.Fixed(m.epoch, 2);
  w.Fixed(m.t_ms, 4);
  w.Fixed(static_cast<uint32_t>(m.x_dm), 4);
  w.Fixed(static_cast<uint32_t>(m.y_dm), 4);
  w.Fixed(m.heading_cdeg, 2);
  w.Fixed(m.speed_cms, 2);
  w.U8(m.acc_m);
  return w.Finish();
}

size_t EncodeBinary(const Route& m, uint8_t* out, size_t capacity) {
  Writer w(out, capacity);
  w.U8(kBinaryVersion);
  w.Fixed(m.route_id, 4);
  w.Fixed(m.version, 2);
  w.U8(m.backend);
  w.Fixed(m.epoch, 2);
  w.U8(m.mode);
  w.U8(m.quantum_dm);
  w.Varint(static_cast<uint32_t>(m.n_points));
  w.Points(m.points, m.n_points);
  w.Varint(static_cast<uint32_t>(m.n_markers));
  for (size_t i = 0; i < m.n_markers; ++i) {
    w.Varint(m.markers[i].point);
    w.U8(m.markers[i].maneuver);
    w.U8(m.markers[i].exit);
  }
  return w.Finish();
}

size_t EncodeBinary(const Traffic& m, uint8_t* out, size_t capacity) {
  Writer w(out, capacity);
  w.U8(kBinaryVersion);
  w.Fixed(m.route_id, 4);
  w.Fixed(m.version, 2);
  w.Varint(static_cast<uint32_t>(m.n_entries));
  for (size_t i = 0; i < m.n_entries; ++i) {
    w.Varint(m.entries[i].offset_m);
    w.Varint(m.entries[i].length_m);
    w.U8(m.entries[i].level);
  }
  return w.Finish();
}

size_t EncodeBinary(const Chunk& m, uint8_t* out, size_t capacity) {
  if (!ShapesFit(m.lines, m.n_lines, m.n_points) || !ShapesFit(m.polys, m.n_polys, m.n_points)) {
    return 0;
  }
  Writer w(out, capacity);
  w.U8(kBinaryVersion);
  WriteKey(w, m.key);
  w.U8(m.provenance);
  w.Fixed(m.epoch, 2);
  w.Fixed(static_cast<uint32_t>(m.origin_x_m), 4);
  w.Fixed(static_cast<uint32_t>(m.origin_y_m), 4);
  w.U8(m.quantum_dm);
  w.Fixed(m.extent, 2);
  WriteShapes(w, m.lines, m.n_lines, m.points);
  WriteShapes(w, m.polys, m.n_polys, m.points);
  return w.Finish();
}

size_t EncodeBinary(const ChunkHave& m, uint8_t* out, size_t capacity) {
  Writer w(out, capacity);
  w.U8(kBinaryVersion);
  w.Fixed(m.epoch, 2);
  w.Varint(static_cast<uint32_t>(m.n_entries));
  for (size_t i = 0; i < m.n_entries; ++i) {
    WriteKey(w, m.entries[i]);
  }
  return w.Finish();
}

}  // namespace sarathi::proto

// NOLINTEND(cppcoreguidelines-pro-bounds-pointer-arithmetic)
