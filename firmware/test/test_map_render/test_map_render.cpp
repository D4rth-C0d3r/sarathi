// Map rasteriser and renderer (core/raster, core/map_render): pixel rules, clipping, heading-up
// projection, and the recorded Bengaluru chunks (docs/fixtures/map) drawn whole and in strips.
// Set SARATHI_MAP_PPM=path to save the rendered frame for a look.

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <vector>

#include <unity.h>

#include "binary_codec.h"
#include "chunk_store.h"
#include "map_render.h"
#include "raster.h"

using sarathi::core::Canvas;
using sarathi::core::ChunkHeader;
using sarathi::core::ChunkId;
using sarathi::core::ChunkStore;
using sarathi::core::ClipRect;
using sarathi::core::MapCamera;
using sarathi::core::MapRenderer;
using sarathi::core::MapRenderStats;
using sarathi::core::MapStyle;
using sarathi::core::ScreenPoint;
using sarathi::core::StoreResult;

// Unity calls these by name, so they need external linkage and Unity's spelling.
void setUp() {}     // NOLINT(misc-use-internal-linkage,readability-identifier-naming)
void tearDown() {}  // NOLINT(misc-use-internal-linkage,readability-identifier-naming)

namespace {

namespace proto = sarathi::proto;
constexpr uint16_t kBg = 0x0000;
constexpr uint16_t kInk = 0xFFFF;
constexpr uint16_t kUntouched = 0x1234;

struct Image {
  int32_t w, h;
  std::vector<uint16_t> px;
  Image(int32_t width, int32_t height) : w(width), h(height), px(width * height, kUntouched) {}
  Canvas Full() { return Canvas{px.data(), w, 0, 0, ClipRect{0, 0, w - 1, h - 1}}; }
  uint16_t At(int32_t x, int32_t y) const { return px[y * w + x]; }
  int Count(uint16_t c) const {
    int n = 0;
    for (uint16_t p : px) n += p == c ? 1 : 0;
    return n;
  }
};

void test_horizontal_line_has_its_width() {
  Image img(20, 20);
  Canvas c = img.Full();
  sarathi::core::DrawSegment(c, {2, 10}, {17, 10}, 3, kInk);
  TEST_ASSERT_EQUAL(16 * 3, img.Count(kInk));
  TEST_ASSERT_EQUAL_HEX16(kInk, img.At(2, 9));
  TEST_ASSERT_EQUAL_HEX16(kInk, img.At(17, 11));
  TEST_ASSERT_EQUAL_HEX16(kUntouched, img.At(10, 12));
}

void test_diagonal_line_keeps_its_width() {
  Image img(40, 40);
  Canvas c = img.Full();
  sarathi::core::DrawSegment(c, {5, 5}, {34, 34}, 3, kInk);
  // Spans of 3 * sqrt(2) ~ 4 pixels across each of the 30 columns.
  TEST_ASSERT_EQUAL(30 * 4, img.Count(kInk));
}

void test_clipping_never_writes_outside() {
  Image img(30, 30);
  Canvas c{img.px.data(), 30, 0, 0, ClipRect{10, 10, 19, 19}};
  sarathi::core::DrawSegment(c, {-100, 15}, {200, 15}, 5, kInk);
  sarathi::core::DrawSegment(c, {15, -50}, {15, 400}, 7, kInk);
  sarathi::core::DrawSegment(c, {-1000000, -1000000}, {1000000, 1000000}, 3, kInk);
  for (int32_t y = 0; y < 30; ++y) {
    for (int32_t x = 0; x < 30; ++x) {
      const bool inside = x >= 10 && x <= 19 && y >= 10 && y <= 19;
      if (!inside) TEST_ASSERT_EQUAL_HEX16(kUntouched, img.At(x, y));
    }
  }
  TEST_ASSERT_EQUAL_HEX16(kInk, img.At(10, 15));  // the line reaches the clip edge
}

void test_buffer_offset() {
  // A strip buffer covering screen rows 100-109.
  Image img(50, 10);
  Canvas c{img.px.data(), 50, 0, 100, ClipRect{0, 100, 49, 109}};
  sarathi::core::DrawSegment(c, {0, 104}, {49, 104}, 1, kInk);
  TEST_ASSERT_EQUAL_HEX16(kInk, img.At(25, 4));
  TEST_ASSERT_EQUAL(50, img.Count(kInk));
}

void test_polygon_fill() {
  Image img(20, 20);
  Canvas c = img.Full();
  const ScreenPoint square[] = {{2, 2}, {12, 2}, {12, 12}, {2, 12}};
  sarathi::core::FillPolygon(c, square, 4, kInk);
  TEST_ASSERT_EQUAL(100, img.Count(kInk));  // [2, 12) on both axes
  const ScreenPoint tri[] = {{0, 0}, {19, 0}, {0, 19}};
  Image t(20, 20);
  Canvas tc = t.Full();
  sarathi::core::FillPolygon(tc, tri, 3, kInk);
  TEST_ASSERT_INT_WITHIN(20, 190, t.Count(kInk));  // half the square
}

void test_heading_up_projection() {
  MapCamera cam;
  cam.x_m = 1000;
  cam.y_m = 2000;
  cam.metres_per_px = 2;
  // Heading north: 100 m north is 50 px up, 100 m east is 50 px right.
  ScreenPoint p = sarathi::core::ProjectMetres(cam, 1000, 2100);
  TEST_ASSERT_EQUAL(cam.anchor_x, p.x);
  TEST_ASSERT_EQUAL(cam.anchor_y - 50, p.y);
  p = sarathi::core::ProjectMetres(cam, 1100, 2000);
  TEST_ASSERT_EQUAL(cam.anchor_x + 50, p.x);
  // Heading east: east is up, north is left.
  cam.heading_deg = 90;
  p = sarathi::core::ProjectMetres(cam, 1100, 2000);
  TEST_ASSERT_EQUAL(cam.anchor_x, p.x);
  TEST_ASSERT_EQUAL(cam.anchor_y - 50, p.y);
  p = sarathi::core::ProjectMetres(cam, 1000, 2100);
  TEST_ASSERT_EQUAL(cam.anchor_x - 50, p.x);
}

// ---- The recorded city ----

std::vector<uint8_t> g_arena(4 * 1024 * 1024);

void LoadCity(ChunkStore& store, const char* set, const std::vector<std::string>& tiles) {
  std::vector<proto::ChunkShape> shapes(proto::kMaxChunkLines + proto::kMaxChunkPolys);
  std::vector<proto::Point> points(proto::kMaxChunkPoints);
  for (const std::string& tile : tiles) {
    const std::string path =
        std::string(SARATHI_MAP_FIXTURES_DIR) + "/chunks/" + set + "/" + tile + ".bin";
    std::ifstream in(path, std::ios::binary);
    const std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(in)), {});
    proto::Chunk c;
    TEST_ASSERT_EQUAL(0, static_cast<int>(proto::DecodeChunk(bytes.data(), bytes.size(), &c,
                                                             {shapes.data(), shapes.size()},
                                                             {points.data(), points.size()})));
    ChunkHeader h;
    h.id = ChunkId{c.key.level, c.key.tile_x, c.key.tile_y, c.key.data_version};
    h.origin_x_m = c.origin_x_m;
    h.origin_y_m = c.origin_y_m;
    h.quantum_dm = c.quantum_dm;
    h.extent = c.extent;
    h.n_lines = static_cast<uint16_t>(c.n_lines);
    h.n_polys = static_cast<uint16_t>(c.n_polys);
    h.n_points = static_cast<uint32_t>(c.n_points);
    StoreResult r{};
    TEST_ASSERT_TRUE(store.Begin(h, nullptr, 0, &r));
    for (size_t i = 0; i < c.n_lines + c.n_polys; ++i) {
      const proto::ChunkShape& s = i < c.n_lines ? c.lines[i] : c.polys[i - c.n_lines];
      TEST_ASSERT_TRUE(store.SetShape(static_cast<uint16_t>(i), s.kind, s.first, s.count));
    }
    for (size_t k = 0; k < c.n_points; ++k) {
      TEST_ASSERT_TRUE(store.SetPoint(static_cast<uint32_t>(k), c.points[k].x, c.points[k].y));
    }
    TEST_ASSERT_EQUAL(0, static_cast<int>(store.Commit()));
  }
}

MapStyle TestStyle() {
  MapStyle s;
  s.background = 0x0841;
  s.water = 0x1A7B;
  s.park = 0x1B05;
  const uint16_t road[] = {0xFA08, 0xFC08, 0xFE60, 0xFFEC, 0xCE79, 0x8C51, 0x52AA};
  const uint8_t px[] = {6, 6, 5, 4, 3, 2, 1};
  for (int i = 0; i < sarathi::core::kRoadClasses; ++i) {
    s.road[i] = road[i];
    s.road_px[i] = px[i];
  }
  return s;
}

MapCamera CityCamera() {
  MapCamera cam;
  cam.x_m = 300;  // near the frame origin (12.97, 77.59): Bengaluru centre
  cam.y_m = -200;
  cam.heading_deg = 33;
  cam.metres_per_px = 2;
  return cam;
}

const std::vector<std::string> kCity = {"14-11722-7595", "14-11722-7596", "14-11722-7597",
                                        "14-11723-7595", "14-11723-7596", "14-11723-7597",
                                        "14-11724-7595", "14-11724-7596", "14-11724-7597"};

void SavePpm(const Image& img) {
  const char* path = std::getenv("SARATHI_MAP_PPM");
  if (path == nullptr) return;
  std::ofstream out(path, std::ios::binary);
  out << "P6\n" << img.w << " " << img.h << "\n255\n";
  for (uint16_t p : img.px) {
    const char rgb[] = {static_cast<char>((p >> 11) << 3), static_cast<char>(((p >> 5) & 63) << 2),
                        static_cast<char>((p & 31) << 3)};
    out.write(rgb, 3);
  }
}

void test_city_in_strips_matches_one_frame() {
  auto store = std::make_unique<ChunkStore>(g_arena.data(), g_arena.size());
  LoadCity(*store, "bengaluru", kCity);
  auto renderer = std::make_unique<MapRenderer>();
  const MapStyle style = TestStyle();
  const MapCamera cam = CityCamera();

  Image whole(480, 480);
  Canvas full = whole.Full();
  const auto start = std::chrono::steady_clock::now();
  constexpr int kRuns = 20;
  MapRenderStats stats;
  for (int i = 0; i < kRuns; ++i) stats = renderer->Render(*store, 0, cam, style, full, i);
  const double ms =
      std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count() /
      kRuns;
  char msg[160];
  std::snprintf(msg, sizeof msg, "city frame: %u chunks, %u shapes, %u segments, %.2f ms on host",
                stats.chunks, stats.shapes, stats.segments, ms);
  TEST_MESSAGE(msg);
  TEST_ASSERT_TRUE(stats.chunks >= 1);
  TEST_ASSERT_TRUE(stats.segments > 300);  // a dense city fills the screen
  TEST_ASSERT_EQUAL(0, whole.Count(kUntouched));
  SavePpm(whole);

  // LVGL draws in strips: 24 strips of 20 rows into a strip-sized buffer.
  Image strips(480, 480);
  std::vector<uint16_t> strip(480 * 20);
  for (int32_t top = 0; top < 480; top += 20) {
    Canvas c{strip.data(), 480, 0, top, ClipRect{0, top, 479, top + 19}};
    renderer->Render(*store, 0, cam, style, c, 99);
    std::copy(strip.begin(), strip.end(), strips.px.begin() + top * 480);
  }
  int differ = 0;
  for (size_t i = 0; i < whole.px.size(); ++i) differ += whole.px[i] != strips.px[i] ? 1 : 0;
  TEST_ASSERT_EQUAL(0, differ);
}

void test_highway_level_and_far_chunks() {
  auto store = std::make_unique<ChunkStore>(g_arena.data(), g_arena.size());
  LoadCity(*store, "expressway", {"12-2881-1830", "12-2882-1830", "12-2883-1830", "14-11530-7322"});
  auto renderer = std::make_unique<MapRenderer>();
  MapCamera cam;
  cam.metres_per_px = 10;  // highway zoom: L1
  Image img(480, 480);
  Canvas c = img.Full();
  const MapRenderStats l1 = renderer->Render(*store, 1, cam, TestStyle(), c, 1);
  TEST_ASSERT_TRUE(l1.chunks >= 1);
  TEST_ASSERT_TRUE(l1.segments > 0);
  // Far from every chunk: nothing but background.
  cam.x_m = 500000;
  const MapRenderStats none = renderer->Render(*store, 1, cam, TestStyle(), c, 2);
  TEST_ASSERT_EQUAL(0, none.chunks);
  TEST_ASSERT_EQUAL(480 * 480, img.Count(TestStyle().background));
}

}  // namespace

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_horizontal_line_has_its_width);
  RUN_TEST(test_diagonal_line_keeps_its_width);
  RUN_TEST(test_clipping_never_writes_outside);
  RUN_TEST(test_buffer_offset);
  RUN_TEST(test_polygon_fill);
  RUN_TEST(test_heading_up_projection);
  RUN_TEST(test_city_in_strips_matches_one_frame);
  RUN_TEST(test_highway_level_and_far_chunks);
  return UNITY_END();
}
