// Chunk store (core/chunk_store): the recorded Bengaluru chunks (docs/fixtures/map), decoded
// with sarathi_proto and copied in the way the link does; eviction and validation.

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include <unity.h>

#include "binary_codec.h"
#include "chunk_store.h"

using sarathi::core::ChunkHeader;
using sarathi::core::ChunkId;
using sarathi::core::ChunkStore;
using sarathi::core::MapShape;
using sarathi::core::ProtectedArea;
using sarathi::core::StoredChunk;
using sarathi::core::StoreResult;

// Unity calls these by name, so they need external linkage and Unity's spelling.
void setUp() {}     // NOLINT(misc-use-internal-linkage,readability-identifier-naming)
void tearDown() {}  // NOLINT(misc-use-internal-linkage,readability-identifier-naming)

namespace {

namespace proto = sarathi::proto;

const char* const kTiles[] = {"14-11722-7595", "14-11722-7596", "14-11722-7597",
                              "14-11723-7595", "14-11723-7596", "14-11723-7597",
                              "14-11724-7595", "14-11724-7596", "14-11724-7597"};

struct Decoded {
  proto::Chunk chunk;
  std::vector<proto::ChunkShape> shapes;
  std::vector<proto::Point> points;
};

Decoded Load(const char* name) {
  const std::string path =
      std::string(SARATHI_MAP_FIXTURES_DIR) + "/chunks/bengaluru/" + name + ".bin";
  std::ifstream in(path, std::ios::binary);
  const std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(in)), {});
  TEST_ASSERT_FALSE_MESSAGE(bytes.empty(), path.c_str());
  Decoded d;
  d.shapes.resize(proto::kMaxChunkLines + proto::kMaxChunkPolys);
  d.points.resize(proto::kMaxChunkPoints);
  const proto::BinError err =
      proto::DecodeChunk(bytes.data(), bytes.size(), &d.chunk, {d.shapes.data(), d.shapes.size()},
                         {d.points.data(), d.points.size()});
  TEST_ASSERT_EQUAL(static_cast<int>(proto::BinError::kNone), static_cast<int>(err));
  return d;
}

ChunkHeader HeaderOf(const proto::Chunk& c) {
  ChunkHeader h;
  h.id = ChunkId{c.key.level, c.key.tile_x, c.key.tile_y, c.key.data_version};
  h.origin_x_m = c.origin_x_m;
  h.origin_y_m = c.origin_y_m;
  h.quantum_dm = c.quantum_dm;
  h.extent = c.extent;
  h.n_lines = static_cast<uint16_t>(c.n_lines);
  h.n_polys = static_cast<uint16_t>(c.n_polys);
  h.n_points = static_cast<uint32_t>(c.n_points);
  return h;
}

// What the link does with a decoded chunk.
StoreResult Insert(ChunkStore& store, const proto::Chunk& c, const ProtectedArea* keep = nullptr,
                   size_t n_keep = 0) {
  StoreResult result{};
  if (!store.Begin(HeaderOf(c), keep, n_keep, &result)) return result;
  for (size_t i = 0; i < c.n_lines + c.n_polys; ++i) {
    const proto::ChunkShape& s = i < c.n_lines ? c.lines[i] : c.polys[i - c.n_lines];
    TEST_ASSERT_TRUE(store.SetShape(static_cast<uint16_t>(i), s.kind, s.first, s.count));
  }
  for (size_t k = 0; k < c.n_points; ++k) {
    TEST_ASSERT_TRUE(store.SetPoint(static_cast<uint32_t>(k), c.points[k].x, c.points[k].y));
  }
  return store.Commit();
}

std::vector<uint8_t> g_arena(4 * 1024 * 1024);

void test_stores_the_city_set() {
  ChunkStore store(g_arena.data(), g_arena.size());
  size_t points = 0;
  for (const char* name : kTiles) {
    const Decoded d = Load(name);
    points += d.chunk.n_points;
    TEST_ASSERT_EQUAL(static_cast<int>(StoreResult::kStored),
                      static_cast<int>(Insert(store, d.chunk)));
    TEST_ASSERT_TRUE(store.Contains(HeaderOf(d.chunk).id));
  }
  TEST_ASSERT_EQUAL(9, store.Count());
  char msg[96];
  std::snprintf(msg, sizeof msg, "Bengaluru 3x3: %zu points, %zu KB stored", points,
                store.UsedBytes() / 1024);
  TEST_MESSAGE(msg);
  // Every shape's box holds its points, and the chunk box holds the shapes (in metres).
  for (size_t i = 0; i < store.Count(); ++i) {
    const StoredChunk c = store.At(i);
    const auto check = [&](const MapShape* shapes, uint16_t n) {
      for (uint16_t s = 0; s < n; ++s) {
        for (uint32_t k = shapes[s].first; k < uint32_t{shapes[s].first} + shapes[s].count; ++k) {
          TEST_ASSERT_TRUE(c.points[k].x >= shapes[s].min_x && c.points[k].x <= shapes[s].max_x);
          TEST_ASSERT_TRUE(c.points[k].y >= shapes[s].min_y && c.points[k].y <= shapes[s].max_y);
        }
        const int32_t x_m = c.origin_x_m + shapes[s].min_x * c.quantum_dm / 10;
        TEST_ASSERT_TRUE(x_m >= c.box.min_x && x_m <= c.box.max_x);
      }
    };
    check(c.lines, c.n_lines);
    check(c.polys, c.n_polys);
    TEST_ASSERT_TRUE(c.box.max_x - c.box.min_x > 2000);  // a z14 tile is about 2.4 km here
    TEST_ASSERT_TRUE(c.box.max_x - c.box.min_x < 2600);
  }
}

void test_evicts_the_least_recently_drawn() {
  const Decoded a = Load(kTiles[0]);
  const Decoded b = Load(kTiles[1]);
  const Decoded c = Load(kTiles[2]);
  const size_t room = ChunkStore::BytesFor(static_cast<uint32_t>(a.chunk.n_lines + a.chunk.n_polys),
                                           static_cast<uint32_t>(a.chunk.n_points)) +
                      ChunkStore::BytesFor(static_cast<uint32_t>(b.chunk.n_lines + b.chunk.n_polys),
                                           static_cast<uint32_t>(b.chunk.n_points));
  ChunkStore store(g_arena.data(), room);  // room for a and b only
  TEST_ASSERT_EQUAL(static_cast<int>(StoreResult::kStored),
                    static_cast<int>(Insert(store, a.chunk)));
  TEST_ASSERT_EQUAL(static_cast<int>(StoreResult::kStored),
                    static_cast<int>(Insert(store, b.chunk)));
  store.MarkDrawn(0, 10);  // a drawn recently, b long ago
  store.MarkDrawn(1, 2);
  TEST_ASSERT_EQUAL(static_cast<int>(StoreResult::kStored),
                    static_cast<int>(Insert(store, c.chunk)));
  TEST_ASSERT_TRUE(store.Contains(HeaderOf(a.chunk).id));
  TEST_ASSERT_FALSE(store.Contains(HeaderOf(b.chunk).id));
  TEST_ASSERT_TRUE(store.Contains(HeaderOf(c.chunk).id));
  ChunkId gone[4];
  TEST_ASSERT_EQUAL(1, store.TakeEvicted(gone, 4));
  TEST_ASSERT_EQUAL_UINT32(HeaderOf(b.chunk).id.tile_y, gone[0].tile_y);
  TEST_ASSERT_EQUAL(0, store.TakeEvicted(gone, 4));
  // The survivors' data moved with compaction and is intact.
  for (size_t i = 0; i < store.Count(); ++i) {
    const StoredChunk s = store.At(i);
    const Decoded& want = s.id.tile_y == HeaderOf(a.chunk).id.tile_y ? a : c;
    TEST_ASSERT_EQUAL(want.chunk.n_points, s.n_points);
    TEST_ASSERT_EQUAL(want.chunk.points[want.chunk.n_points - 1].x, s.points[s.n_points - 1].x);
  }
}

void test_never_evicts_near_the_rider() {
  const Decoded a = Load(kTiles[0]);
  const Decoded b = Load(kTiles[1]);
  // Room for one chunk, either of the two.
  const auto bytes = [](const proto::Chunk& c) {
    return ChunkStore::BytesFor(static_cast<uint32_t>(c.n_lines + c.n_polys),
                                static_cast<uint32_t>(c.n_points));
  };
  ChunkStore store(g_arena.data(), std::max(bytes(a.chunk), bytes(b.chunk)));
  TEST_ASSERT_EQUAL(static_cast<int>(StoreResult::kStored),
                    static_cast<int>(Insert(store, a.chunk)));
  const StoredChunk held = store.At(0);
  const ProtectedArea rider{(held.box.min_x + held.box.max_x) / 2, held.box.min_y - 500, 1000};
  TEST_ASSERT_EQUAL(static_cast<int>(StoreResult::kFull),
                    static_cast<int>(Insert(store, b.chunk, &rider, 1)));
  TEST_ASSERT_TRUE(store.Contains(held.id));
  const ProtectedArea far_away{held.box.max_x + 5000, held.box.max_y + 5000, 1000};
  TEST_ASSERT_EQUAL(static_cast<int>(StoreResult::kStored),
                    static_cast<int>(Insert(store, b.chunk, &far_away, 1)));
}

void test_a_new_version_replaces_the_tile() {
  const Decoded a = Load(kTiles[4]);
  ChunkStore store(g_arena.data(), g_arena.size());
  TEST_ASSERT_EQUAL(static_cast<int>(StoreResult::kStored),
                    static_cast<int>(Insert(store, a.chunk)));
  proto::Chunk newer = a.chunk;
  newer.key.data_version += 1;
  TEST_ASSERT_EQUAL(static_cast<int>(StoreResult::kStored), static_cast<int>(Insert(store, newer)));
  TEST_ASSERT_EQUAL(1, store.Count());
  TEST_ASSERT_TRUE(store.Contains(HeaderOf(newer).id));
  TEST_ASSERT_FALSE(store.Contains(HeaderOf(a.chunk).id));
  ChunkId gone[1];
  TEST_ASSERT_EQUAL(0, store.TakeEvicted(gone, 1));  // replaced, not evicted
}

void test_refuses_bad_chunks() {
  ChunkStore store(g_arena.data(), 64 * 1024);
  ChunkHeader h;
  h.extent = 1000;
  h.n_lines = 1;
  h.n_points = 2;
  StoreResult r{};
  TEST_ASSERT_TRUE(store.Begin(h, nullptr, 0, &r));
  TEST_ASSERT_FALSE(store.SetPoint(0, 1000 + 65, 0));  // past the overscan
  TEST_ASSERT_FALSE(store.SetShape(0, 5, 1, 2));       // past the points
  TEST_ASSERT_TRUE(store.SetPoint(0, -64, 1064));
  TEST_ASSERT_TRUE(store.SetPoint(1, 3, 4));
  // The shape was never set.
  TEST_ASSERT_EQUAL(static_cast<int>(StoreResult::kInvalid), static_cast<int>(store.Commit()));
  TEST_ASSERT_EQUAL(0, store.Count());
  TEST_ASSERT_EQUAL(0, store.UsedBytes());
  h.extent = 40000;  // wouldn't fit int16
  TEST_ASSERT_FALSE(store.Begin(h, nullptr, 0, &r));
  TEST_ASSERT_EQUAL(static_cast<int>(StoreResult::kInvalid), static_cast<int>(r));
  h.extent = 1000;
  h.n_points = 100000;  // bigger than the arena
  TEST_ASSERT_FALSE(store.Begin(h, nullptr, 0, &r));
}

void test_clear_drops_everything() {
  ChunkStore store(g_arena.data(), g_arena.size());
  TEST_ASSERT_EQUAL(static_cast<int>(StoreResult::kStored),
                    static_cast<int>(Insert(store, Load(kTiles[3]).chunk)));
  store.Clear();
  TEST_ASSERT_EQUAL(0, store.Count());
  TEST_ASSERT_EQUAL(0, store.UsedBytes());
}

}  // namespace

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_stores_the_city_set);
  RUN_TEST(test_evicts_the_least_recently_drawn);
  RUN_TEST(test_never_evicts_near_the_rider);
  RUN_TEST(test_a_new_version_replaces_the_tile);
  RUN_TEST(test_refuses_bad_chunks);
  RUN_TEST(test_clear_drops_everything);
  return UNITY_END();
}
