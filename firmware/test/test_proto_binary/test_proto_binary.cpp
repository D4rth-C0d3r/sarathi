// Binary messages against the golden vectors (docs/protocol/vectors/binary_messages.json):
// each valid vector's `decoded` content encodes to its bytes, and the bytes decode back to it
// (pose, route, traffic and chunk: the device never decodes chunk_have); each invalid vector
// fails for the expected reason.

#include <cstdint>
#include <string>
#include <vector>

#include <unity.h>

#include "../protocol_vectors.h"
#include "binary_codec.h"
#include "messages.h"

using protocol_vectors::Doc;
using protocol_vectors::Elements;
using protocol_vectors::Hex;
using protocol_vectors::Integer;
using protocol_vectors::Need;
using protocol_vectors::Text;
using namespace sarathi::proto;  // NOLINT(google-build-using-namespace)

// Unity calls these by name, so they need external linkage and Unity's spelling.
void setUp() {}     // NOLINT(misc-use-internal-linkage,readability-identifier-naming)
void tearDown() {}  // NOLINT(misc-use-internal-linkage,readability-identifier-naming)

namespace {

template <typename T>
T Num(const Doc& doc, int object, const char* name) {
  return static_cast<T>(Integer(doc, Need(doc, object, name)));
}

template <typename T>
T At(const Doc& doc, int array, size_t i) {
  return static_cast<T>(Integer(doc, Elements(doc, array).at(i)));
}

void AppendPoints(const Doc& doc, int array, std::vector<Point>* out) {
  for (const int p : Elements(doc, array)) {
    out->push_back(Point{At<int32_t>(doc, p, 0), At<int32_t>(doc, p, 1)});
  }
}

// Decoded vector content, with the storage the structs point into.
struct Expected {
  Pose pose;
  Route route;
  Traffic traffic;
  Chunk chunk;
  ChunkHave have;
  std::vector<Point> points;
  std::vector<RouteMarker> markers;
  std::vector<TrafficEntry> entries;
  std::vector<ChunkShape> shapes;
  std::vector<ChunkKey> keys;
};

void ReadShapes(const Doc& doc, int array, Expected* e) {
  for (const int s : Elements(doc, array)) {
    const auto first = static_cast<uint32_t>(e->points.size());
    AppendPoints(doc, Elements(doc, s).at(1), &e->points);
    e->shapes.push_back(
        ChunkShape{At<uint8_t>(doc, s, 0), static_cast<uint16_t>(e->points.size() - first), first});
  }
}

// Fills the struct for `type` from the vector's `decoded` object.
void Build(const Doc& doc, uint8_t type, int d, Expected* e) {
  switch (static_cast<MsgType>(type)) {
    case MsgType::kPose:
      e->pose = Pose{Num<uint16_t>(doc, d, "epoch"),        Num<uint32_t>(doc, d, "t_ms"),
                     Num<int32_t>(doc, d, "x_dm"),          Num<int32_t>(doc, d, "y_dm"),
                     Num<uint16_t>(doc, d, "heading_cdeg"), Num<uint16_t>(doc, d, "speed_cms"),
                     Num<uint8_t>(doc, d, "acc_m")};
      return;
    case MsgType::kRoute: {
      AppendPoints(doc, Need(doc, d, "points"), &e->points);
      for (const int m : Elements(doc, Need(doc, d, "markers"))) {
        e->markers.push_back(
            RouteMarker{At<uint16_t>(doc, m, 0), At<uint8_t>(doc, m, 1), At<uint8_t>(doc, m, 2)});
      }
      Route& r = e->route;
      r.route_id = Num<uint32_t>(doc, d, "route_id");
      r.version = Num<uint16_t>(doc, d, "version");
      r.backend = Num<uint8_t>(doc, d, "backend");
      r.epoch = Num<uint16_t>(doc, d, "epoch");
      r.mode = Num<uint8_t>(doc, d, "mode");
      r.quantum_dm = Num<uint8_t>(doc, d, "quantum_dm");
      r.points = e->points.data();
      r.n_points = e->points.size();
      r.markers = e->markers.data();
      r.n_markers = e->markers.size();
      return;
    }
    case MsgType::kTraffic:
      for (const int t : Elements(doc, Need(doc, d, "entries"))) {
        e->entries.push_back(
            TrafficEntry{At<uint32_t>(doc, t, 0), At<uint32_t>(doc, t, 1), At<uint8_t>(doc, t, 2)});
      }
      e->traffic = Traffic{Num<uint32_t>(doc, d, "route_id"), Num<uint16_t>(doc, d, "version"),
                           e->entries.data(), e->entries.size()};
      return;
    case MsgType::kChunk: {
      Chunk& c = e->chunk;
      c.key = ChunkKey{Num<uint8_t>(doc, d, "level"), Num<uint32_t>(doc, d, "tile_x"),
                       Num<uint32_t>(doc, d, "tile_y"), Num<uint32_t>(doc, d, "data_version")};
      c.provenance = Num<uint8_t>(doc, d, "provenance");
      c.epoch = Num<uint16_t>(doc, d, "epoch");
      c.origin_x_m = Num<int32_t>(doc, d, "origin_x_m");
      c.origin_y_m = Num<int32_t>(doc, d, "origin_y_m");
      c.quantum_dm = Num<uint8_t>(doc, d, "quantum_dm");
      c.extent = Num<uint16_t>(doc, d, "extent");
      ReadShapes(doc, Need(doc, d, "lines"), e);
      c.n_lines = e->shapes.size();
      ReadShapes(doc, Need(doc, d, "polys"), e);
      c.n_polys = e->shapes.size() - c.n_lines;
      c.lines = e->shapes.data();
      c.polys = e->shapes.data() + c.n_lines;
      c.points = e->points.data();
      c.n_points = e->points.size();
      return;
    }
    case MsgType::kChunkHave:
      for (const int k : Elements(doc, Need(doc, d, "entries"))) {
        e->keys.push_back(ChunkKey{At<uint8_t>(doc, k, 0), At<uint32_t>(doc, k, 1),
                                   At<uint32_t>(doc, k, 2), At<uint32_t>(doc, k, 3)});
      }
      e->have = ChunkHave{Num<uint16_t>(doc, d, "epoch"), e->keys.data(), e->keys.size()};
      return;
    default:
      TEST_FAIL_MESSAGE("not a binary type");
  }
}

size_t Encode(uint8_t type, const Expected& e, std::vector<uint8_t>* out) {
  out->assign(16384, 0);
  switch (static_cast<MsgType>(type)) {
    case MsgType::kPose:
      return EncodeBinary(e.pose, out->data(), out->size());
    case MsgType::kRoute:
      return EncodeBinary(e.route, out->data(), out->size());
    case MsgType::kTraffic:
      return EncodeBinary(e.traffic, out->data(), out->size());
    case MsgType::kChunk:
      return EncodeBinary(e.chunk, out->data(), out->size());
    default:
      return EncodeBinary(e.have, out->data(), out->size());
  }
}

void AssertPoints(const Point* want, const Point* got, size_t n, const char* name) {
  for (size_t i = 0; i < n; ++i) {
    TEST_ASSERT_EQUAL_INT32_MESSAGE(want[i].x, got[i].x, name);  // NOLINT
    TEST_ASSERT_EQUAL_INT32_MESSAGE(want[i].y, got[i].y, name);  // NOLINT
  }
}

void AssertShapes(const ChunkShape* want, const ChunkShape* got, size_t n, const char* name) {
  for (size_t i = 0; i < n; ++i) {
    TEST_ASSERT_EQUAL_MESSAGE(want[i].kind, got[i].kind, name);    // NOLINT
    TEST_ASSERT_EQUAL_MESSAGE(want[i].count, got[i].count, name);  // NOLINT
    TEST_ASSERT_EQUAL_MESSAGE(want[i].first, got[i].first, name);  // NOLINT
  }
}

// Storage the size the device will give each decoder (the protocol maxima).
struct Storage {
  std::vector<Point> points = std::vector<Point>(kMaxChunkPoints);
  std::vector<RouteMarker> markers = std::vector<RouteMarker>(kMaxRouteMarkers);
  std::vector<TrafficEntry> entries = std::vector<TrafficEntry>(kMaxTrafficEntries);
  std::vector<ChunkShape> shapes = std::vector<ChunkShape>(kMaxChunkLines + kMaxChunkPolys);
};

BinError DecodeAny(uint8_t type, const std::vector<uint8_t>& bytes, Storage* s, Expected* got) {
  switch (static_cast<MsgType>(type)) {
    case MsgType::kPose:
      return DecodePose(bytes.data(), bytes.size(), &got->pose);
    case MsgType::kRoute:
      return DecodeRoute(bytes.data(), bytes.size(), &got->route,
                         {s->points.data(), kMaxRoutePoints},
                         {s->markers.data(), s->markers.size()});
    case MsgType::kTraffic:
      return DecodeTraffic(bytes.data(), bytes.size(), &got->traffic,
                           {s->entries.data(), s->entries.size()});
    case MsgType::kChunk:
      return DecodeChunk(bytes.data(), bytes.size(), &got->chunk,
                         {s->shapes.data(), s->shapes.size()},
                         {s->points.data(), s->points.size()});
    default:
      TEST_FAIL_MESSAGE("the device doesn't decode this type");
      return BinError::kInvalid;
  }
}

void AssertDecoded(uint8_t type, const Expected& want, const Expected& got, const char* name) {
  switch (static_cast<MsgType>(type)) {
    case MsgType::kPose: {
      std::vector<uint8_t> a;
      std::vector<uint8_t> b;
      TEST_ASSERT_EQUAL_MESSAGE(kPoseSize, Encode(type, want, &a), name);
      TEST_ASSERT_EQUAL_MESSAGE(kPoseSize, Encode(type, got, &b), name);
      TEST_ASSERT_EQUAL_HEX8_ARRAY_MESSAGE(a.data(), b.data(), kPoseSize, name);
      TEST_ASSERT_EQUAL_MESSAGE(want.pose.acc_m, got.pose.acc_m, name);
      return;
    }
    case MsgType::kRoute: {
      const Route& w = want.route;
      const Route& g = got.route;
      TEST_ASSERT_EQUAL_MESSAGE(w.route_id, g.route_id, name);
      TEST_ASSERT_EQUAL_MESSAGE(w.version, g.version, name);
      TEST_ASSERT_EQUAL_MESSAGE(w.backend, g.backend, name);
      TEST_ASSERT_EQUAL_MESSAGE(w.epoch, g.epoch, name);
      TEST_ASSERT_EQUAL_MESSAGE(w.mode, g.mode, name);
      TEST_ASSERT_EQUAL_MESSAGE(w.quantum_dm, g.quantum_dm, name);
      TEST_ASSERT_EQUAL_MESSAGE(w.n_points, g.n_points, name);
      AssertPoints(w.points, g.points, w.n_points, name);
      TEST_ASSERT_EQUAL_MESSAGE(w.n_markers, g.n_markers, name);
      for (size_t i = 0; i < w.n_markers; ++i) {
        TEST_ASSERT_EQUAL_MESSAGE(w.markers[i].point, g.markers[i].point, name);  // NOLINT
        TEST_ASSERT_EQUAL_MESSAGE(w.markers[i].maneuver, g.markers[i].maneuver,   // NOLINT
                                  name);
        TEST_ASSERT_EQUAL_MESSAGE(w.markers[i].exit, g.markers[i].exit, name);  // NOLINT
      }
      return;
    }
    case MsgType::kTraffic: {
      const Traffic& w = want.traffic;
      const Traffic& g = got.traffic;
      TEST_ASSERT_EQUAL_MESSAGE(w.route_id, g.route_id, name);
      TEST_ASSERT_EQUAL_MESSAGE(w.version, g.version, name);
      TEST_ASSERT_EQUAL_MESSAGE(w.n_entries, g.n_entries, name);
      for (size_t i = 0; i < w.n_entries; ++i) {
        TEST_ASSERT_EQUAL_MESSAGE(w.entries[i].offset_m, g.entries[i].offset_m, name);  // NOLINT
        TEST_ASSERT_EQUAL_MESSAGE(w.entries[i].length_m, g.entries[i].length_m, name);  // NOLINT
        TEST_ASSERT_EQUAL_MESSAGE(w.entries[i].level, g.entries[i].level, name);        // NOLINT
      }
      return;
    }
    case MsgType::kChunk: {
      const Chunk& w = want.chunk;
      const Chunk& g = got.chunk;
      TEST_ASSERT_EQUAL_MESSAGE(w.key.level, g.key.level, name);
      TEST_ASSERT_EQUAL_MESSAGE(w.key.tile_x, g.key.tile_x, name);
      TEST_ASSERT_EQUAL_MESSAGE(w.key.tile_y, g.key.tile_y, name);
      TEST_ASSERT_EQUAL_MESSAGE(w.key.data_version, g.key.data_version, name);
      TEST_ASSERT_EQUAL_MESSAGE(w.provenance, g.provenance, name);
      TEST_ASSERT_EQUAL_MESSAGE(w.epoch, g.epoch, name);
      TEST_ASSERT_EQUAL_MESSAGE(w.origin_x_m, g.origin_x_m, name);
      TEST_ASSERT_EQUAL_MESSAGE(w.origin_y_m, g.origin_y_m, name);
      TEST_ASSERT_EQUAL_MESSAGE(w.quantum_dm, g.quantum_dm, name);
      TEST_ASSERT_EQUAL_MESSAGE(w.extent, g.extent, name);
      TEST_ASSERT_EQUAL_MESSAGE(w.n_lines, g.n_lines, name);
      TEST_ASSERT_EQUAL_MESSAGE(w.n_polys, g.n_polys, name);
      TEST_ASSERT_EQUAL_MESSAGE(w.n_points, g.n_points, name);
      AssertShapes(w.lines, g.lines, w.n_lines, name);
      AssertShapes(w.polys, g.polys, w.n_polys, name);
      AssertPoints(w.points, g.points, w.n_points, name);
      return;
    }
    default:
      return;
  }
}

void TestValidVectors() {
  const Doc doc = protocol_vectors::Load("binary_messages.json");
  Storage storage;
  size_t decoded = 0;
  for (const int c : Elements(doc, Need(doc, 0, "valid"))) {
    const std::string name = Text(doc, Need(doc, c, "name"));
    const auto type = Num<uint8_t>(doc, c, "type");
    const auto bytes = Hex(Text(doc, Need(doc, c, "hex")));
    Expected want;
    Build(doc, type, Need(doc, c, "decoded"), &want);
    std::vector<uint8_t> out;
    const size_t n = Encode(type, want, &out);
    TEST_ASSERT_EQUAL_MESSAGE(bytes.size(), n, name.c_str());
    TEST_ASSERT_EQUAL_HEX8_ARRAY_MESSAGE(bytes.data(), out.data(), n, name.c_str());
    if (type == static_cast<uint8_t>(MsgType::kChunkHave)) {
      continue;
    }
    Expected got;
    TEST_ASSERT_EQUAL_STRING_MESSAGE("none", BinErrorName(DecodeAny(type, bytes, &storage, &got)),
                                     name.c_str());
    AssertDecoded(type, want, got, name.c_str());
    ++decoded;
  }
  TEST_ASSERT_TRUE(decoded >= 8);
}

void TestInvalidVectors() {
  const Doc doc = protocol_vectors::Load("binary_messages.json");
  Storage storage;
  size_t checked = 0;
  for (const int c : Elements(doc, Need(doc, 0, "invalid"))) {
    const auto type = Num<uint8_t>(doc, c, "type");
    if (type == static_cast<uint8_t>(MsgType::kChunkHave)) {
      continue;
    }
    const std::string name = Text(doc, Need(doc, c, "name"));
    Expected got;
    TEST_ASSERT_EQUAL_STRING_MESSAGE(
        Text(doc, Need(doc, c, "error")).c_str(),
        BinErrorName(DecodeAny(type, Hex(Text(doc, Need(doc, c, "hex"))), &storage, &got)),
        name.c_str());
    ++checked;
  }
  TEST_ASSERT_TRUE(checked >= 9);
}

// A valid message larger than the caller's storage is reported as such, not as invalid.
void TestNoRoom() {
  const std::vector<Point> points = {{0, 0}, {5, 5}, {10, 0}};
  const RouteMarker marker{1, static_cast<uint8_t>(Maneuver::kLeft), 0};
  const Route route{1, 0, 0, 7, 0, 10, points.data(), points.size(), &marker, 1};
  std::vector<uint8_t> bytes(64);
  bytes.resize(EncodeBinary(route, bytes.data(), bytes.size()));
  TEST_ASSERT_TRUE(!bytes.empty());
  std::vector<Point> two(2);
  RouteMarker markers[1];
  Route out;
  TEST_ASSERT_EQUAL_STRING("no_room",
                           BinErrorName(DecodeRoute(bytes.data(), bytes.size(), &out,
                                                    {two.data(), two.size()}, {markers, 1})));
  std::vector<Point> three(3);  // the points fit, the marker doesn't
  TEST_ASSERT_EQUAL_STRING("no_room",
                           BinErrorName(DecodeRoute(bytes.data(), bytes.size(), &out,
                                                    {three.data(), three.size()}, {markers, 0})));
  TEST_ASSERT_EQUAL_STRING("none",
                           BinErrorName(DecodeRoute(bytes.data(), bytes.size(), &out,
                                                    {three.data(), three.size()}, {markers, 1})));
  TEST_ASSERT_EQUAL(static_cast<int>(Maneuver::kLeft), out.markers[0].maneuver);  // NOLINT
}

void TestEncodeLimits() {
  Pose pose;
  std::vector<uint8_t> out(kPoseSize);
  TEST_ASSERT_EQUAL(kPoseSize, EncodeBinary(pose, out.data(), out.size()));
  TEST_ASSERT_EQUAL(0, EncodeBinary(pose, out.data(), kPoseSize - 1));
  // A delta outside 32 bits can't be written.
  const std::vector<Point> far = {{INT32_MAX, 0}, {INT32_MIN, 0}};
  const Route route{1, 0, 0, 7, 0, 10, far.data(), far.size(), nullptr, 0};
  out.assign(64, 0);
  TEST_ASSERT_EQUAL(0, EncodeBinary(route, out.data(), out.size()));
  // A chunk shape outside the point list.
  const ChunkShape line{0, 3, 0};
  Chunk chunk;
  chunk.extent = 100;
  chunk.lines = &line;
  chunk.n_lines = 1;
  chunk.points = far.data();
  chunk.n_points = far.size();
  TEST_ASSERT_EQUAL(0, EncodeBinary(chunk, out.data(), out.size()));
}

// Varints: the largest 5-byte value decodes; a 6th byte or 2^32 doesn't.
void TestVarintEdges() {
  // traffic with one entry at offset 2^32 - 1.
  std::vector<uint8_t> base = {1, 3, 0, 0, 0, 2, 0, 1};
  std::vector<TrafficEntry> entries(1);
  Traffic out;
  auto with = [&base](std::vector<uint8_t> tail) {
    std::vector<uint8_t> v = base;
    v.insert(v.end(), tail.begin(), tail.end());
    return v;
  };
  auto ok = with({0xFF, 0xFF, 0xFF, 0xFF, 0x0F, 0x01, 0x00});
  TEST_ASSERT_EQUAL_STRING(
      "none", BinErrorName(DecodeTraffic(ok.data(), ok.size(), &out, {entries.data(), 1})));
  TEST_ASSERT_EQUAL_UINT32(UINT32_MAX, out.entries[0].offset_m);  // NOLINT
  auto big = with({0xFF, 0xFF, 0xFF, 0xFF, 0x10, 0x01, 0x00});
  TEST_ASSERT_EQUAL_STRING(
      "invalid", BinErrorName(DecodeTraffic(big.data(), big.size(), &out, {entries.data(), 1})));
  auto six = with({0x80, 0x80, 0x80, 0x80, 0x80, 0x00, 0x01, 0x00});
  TEST_ASSERT_EQUAL_STRING(
      "invalid", BinErrorName(DecodeTraffic(six.data(), six.size(), &out, {entries.data(), 1})));
  const std::vector<uint8_t> empty;
  TEST_ASSERT_EQUAL_STRING("invalid", BinErrorName(DecodePose(empty.data(), 0, nullptr)));
}

}  // namespace

int main() {
  UNITY_BEGIN();
  RUN_TEST(TestValidVectors);
  RUN_TEST(TestInvalidVectors);
  RUN_TEST(TestNoRoom);
  RUN_TEST(TestEncodeLimits);
  RUN_TEST(TestVarintEdges);
  return UNITY_END();
}
