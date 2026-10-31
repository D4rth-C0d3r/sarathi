// Binary messages (docs/protocol/protocol.md section 4): decoding pose, route, traffic and
// chunk into caller-provided buffers with every limit checked, and encoding all five binary
// messages. Little-endian; points are zigzag varint deltas.

#pragma once

#include <cstddef>
#include <cstdint>

#include "messages.h"

namespace sarathi::proto {

constexpr uint8_t kBinaryVersion = 1;
constexpr size_t kPoseSize = 20;
constexpr uint16_t kMaxHeadingCdeg = 35999;
constexpr size_t kMaxRoutePoints = 4000;
constexpr size_t kMaxRouteMarkers = 256;
constexpr int32_t kRouteCoordLimit = 1 << 30;  // |x|, |y| below this, in quanta
constexpr size_t kMaxTrafficEntries = 256;
constexpr size_t kMaxChunkLines = 4096;
constexpr size_t kMaxChunkPolys = 1024;
constexpr size_t kMaxShapePoints = 1024;
constexpr size_t kMaxChunkPoints = 8192;
constexpr int32_t kChunkOverscan = 64;  // coordinates may stray this far outside the extent
constexpr size_t kMaxChunkHave = 512;

enum class BinError : uint8_t {
  kNone,
  kVerUnknown,  // a newer layout: skipped
  kInvalid,
  kNoRoom,  // valid, but more than the caller's buffers hold
};

[[nodiscard]] const char* BinErrorName(BinError error);

// Caller-owned storage for a decoder to fill.
template <typename T>
struct Buffer {
  T* data = nullptr;
  size_t capacity = 0;
};

struct Point {
  int32_t x = 0;
  int32_t y = 0;
};

struct Pose {
  uint16_t epoch = 0;
  uint32_t t_ms = 0;
  int32_t x_dm = 0;
  int32_t y_dm = 0;
  uint16_t heading_cdeg = 0;
  uint16_t speed_cms = 0;
  uint8_t acc_m = 255;  // 255 = unknown
};

struct RouteMarker {
  uint16_t point = 0;    // index into the route's points
  uint8_t maneuver = 0;  // Maneuver code; unknown codes are drawn as straight
  uint8_t exit = 0;
};

struct Route {
  uint32_t route_id = 0;
  uint16_t version = 0;
  uint8_t backend = 0;  // Routing
  uint16_t epoch = 0;
  uint8_t mode = 0;  // 0 two-wheeler, 1 driving
  uint8_t quantum_dm = 10;
  const Point* points = nullptr;  // absolute, in quanta from the ride-frame origin
  size_t n_points = 0;
  const RouteMarker* markers = nullptr;
  size_t n_markers = 0;
};

struct TrafficEntry {
  uint32_t offset_m = 0;
  uint32_t length_m = 0;
  uint8_t level = 3;  // 0 free, 1 slow, 2 jam, 3 unknown
};

struct Traffic {
  uint32_t route_id = 0;
  uint16_t version = 0;
  const TrafficEntry* entries = nullptr;
  size_t n_entries = 0;
};

// A road line (kind = class 0-6) or a polygon (kind 0 water, 1 park): `count` points from
// `first` in the chunk's point list.
struct ChunkShape {
  uint8_t kind = 0;
  uint16_t count = 0;
  uint32_t first = 0;
};

struct Chunk {
  ChunkKey key;
  uint8_t provenance = 1;  // 1 osm_openfreemap, 2 osm_protomaps_offline
  uint16_t epoch = 0;
  int32_t origin_x_m = 0;
  int32_t origin_y_m = 0;
  uint8_t quantum_dm = 20;
  uint16_t extent = 0;
  const ChunkShape* lines = nullptr;
  size_t n_lines = 0;
  const ChunkShape* polys = nullptr;
  size_t n_polys = 0;
  const Point* points = nullptr;  // absolute, in quanta from the chunk origin
  size_t n_points = 0;
};

struct ChunkHave {
  uint16_t epoch = 0;
  const ChunkKey* entries = nullptr;
  size_t n_entries = 0;
};

[[nodiscard]] BinError DecodePose(const uint8_t* data, size_t size, Pose* out);
[[nodiscard]] BinError DecodeRoute(const uint8_t* data, size_t size, Route* out,
                                   Buffer<Point> points, Buffer<RouteMarker> markers);
[[nodiscard]] BinError DecodeTraffic(const uint8_t* data, size_t size, Traffic* out,
                                     Buffer<TrafficEntry> entries);
// `shapes` holds the lines followed by the polygons.
[[nodiscard]] BinError DecodeChunk(const uint8_t* data, size_t size, Chunk* out,
                                   Buffer<ChunkShape> shapes, Buffer<Point> points);

// Each returns the length written, or 0 if it needs more than `capacity` bytes or a point
// delta doesn't fit 32 bits (or a chunk shape lies outside its point list). The content is written
// as given, not checked against the limits.
size_t EncodeBinary(const Pose& m, uint8_t* out, size_t capacity);
size_t EncodeBinary(const Route& m, uint8_t* out, size_t capacity);
size_t EncodeBinary(const Traffic& m, uint8_t* out, size_t capacity);
size_t EncodeBinary(const Chunk& m, uint8_t* out, size_t capacity);
size_t EncodeBinary(const ChunkHave& m, uint8_t* out, size_t capacity);

}  // namespace sarathi::proto
