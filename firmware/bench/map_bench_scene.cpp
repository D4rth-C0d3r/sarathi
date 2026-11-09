#include "map_bench_scene.h"

#include <algorithm>
#include <cmath>
#include <new>

#include <lvgl.h>

#include "binary_codec.h"
#include "theme_binding.h"

namespace sarathi::bench {

namespace {

namespace proto = sarathi::proto;
using core::MapStyle;

constexpr float kPi = 3.14159265F;
constexpr float kRadToDeg = 180.0F / kPi;

struct Phase {
  const char* name;
  uint8_t level;
  float metres_per_px;
  bool glow;
  bool city;     // orbit the city, else follow a motorway
  bool on_ghat;  // L0 motorway rather than L1
  float speed_ms;
};

// Bengaluru frame (12.97, 77.59): an orbit through the densest 500 m cells around (-1000, 0).
constexpr float kCityX = -1000;
constexpr float kCityY = 0;
constexpr float kCityR = 700;

constexpr Phase kPhases[] = {
    {"city L0 2 m/px", 0, 2, false, true, false, 12},
    {"city L0 2 m/px glow", 0, 2, true, true, false, 12},
    {"city L0 4 m/px", 0, 4, false, true, false, 12},
    {"highway L1 10 m/px", 1, 10, false, false, false, 28},
    {"ghat L0 2 m/px", 0, 2, false, false, true, 28},
};
constexpr size_t kNumPhases = sizeof(kPhases) / sizeof(kPhases[0]);

MapStyle StyleFor(const Phase& p) {
  MapStyle s;
  s.background = ui::theme::Pixel(0x101418);
  s.water = ui::theme::Pixel(0x1E3A5F);
  s.park = ui::theme::Pixel(0x1E3F2A);
  constexpr core::Rgb kRoad[] = {0xE8743B, 0xF0A040, 0xF2C94C, 0xE0E070,
                                 0xC8C8C8, 0x8C8C8C, 0x5A5A5A};
  constexpr core::Rgb kGlow[] = {0x5A2410, 0x5A3A10, 0x5A4A14, 0x50501C,
                                 0x404040, 0x303030, 0x202020};
  constexpr uint8_t kNear[] = {7, 7, 6, 5, 4, 3, 2};  // 2 m/px
  constexpr uint8_t kMid[] = {5, 5, 4, 3, 2, 2, 1};   // 4 m/px
  constexpr uint8_t kFar[] = {4, 4, 3, 2, 1, 1, 1};   // 10 m/px
  const uint8_t* px = p.metres_per_px <= 2 ? kNear : (p.metres_per_px <= 4 ? kMid : kFar);
  for (int i = 0; i < core::kRoadClasses; ++i) {
    s.road[i] = ui::theme::Pixel(kRoad[i]);  // NOLINT(*-constant-array-index)
    s.glow[i] = ui::theme::Pixel(kGlow[i]);  // NOLINT(*-constant-array-index)
    s.road_px[i] = px[i];                    // NOLINT(*-pointer-arithmetic)
  }
  s.glow_extra_px = p.glow ? 4 : 0;
  return s;
}

bool StoreChunk(core::ChunkStore& store, const proto::Chunk& c) {
  core::ChunkHeader h;
  h.id = core::ChunkId{c.key.level, c.key.tile_x, c.key.tile_y, c.key.data_version};
  h.origin_x_m = c.origin_x_m;
  h.origin_y_m = c.origin_y_m;
  h.quantum_dm = c.quantum_dm;
  h.extent = c.extent;
  h.n_lines = static_cast<uint16_t>(c.n_lines);
  h.n_polys = static_cast<uint16_t>(c.n_polys);
  h.n_points = static_cast<uint32_t>(c.n_points);
  core::StoreResult r{};
  if (!store.Begin(h, nullptr, 0, &r)) {
    return false;
  }
  for (size_t i = 0; i < c.n_lines + c.n_polys; ++i) {
    // NOLINTNEXTLINE(*-pointer-arithmetic)
    const proto::ChunkShape& s = i < c.n_lines ? c.lines[i] : c.polys[i - c.n_lines];
    if (!store.SetShape(static_cast<uint16_t>(i), s.kind, s.first, s.count)) {
      store.Abort();
      return false;
    }
  }
  for (size_t k = 0; k < c.n_points; ++k) {
    // NOLINTNEXTLINE(*-pointer-arithmetic)
    if (!store.SetPoint(static_cast<uint32_t>(k), c.points[k].x, c.points[k].y)) {
      store.Abort();
      return false;
    }
  }
  return store.Commit() == core::StoreResult::kStored;
}

}  // namespace

size_t LoadChunks(core::ChunkStore& store, uint8_t* scratch, const ChunkBlob* blobs,
                  size_t n_blobs) {
  constexpr size_t kShapes = proto::kMaxChunkLines + proto::kMaxChunkPolys;
  auto* shapes = reinterpret_cast<proto::ChunkShape*>(scratch);  // NOLINT(*-reinterpret-cast)
  auto* points = reinterpret_cast<proto::Point*>(                // NOLINT(*-reinterpret-cast)
      scratch + kShapes * sizeof(proto::ChunkShape));            // NOLINT(*-pointer-arithmetic)
  const size_t n_points =
      (MapBenchScene::kScratchBytes - kShapes * sizeof(proto::ChunkShape)) / sizeof(proto::Point);
  for (size_t i = 0; i < n_blobs; ++i) {
    proto::Chunk c;
    // NOLINTNEXTLINE(*-pointer-arithmetic)
    if (proto::DecodeChunk(blobs[i].data, blobs[i].size, &c, {shapes, kShapes},
                           {points, n_points}) == proto::BinError::kNone) {
      (void)StoreChunk(store, c);
    }
  }
  return store.Count();
}

size_t MapBenchScene::Init(uint8_t* arena, size_t arena_bytes, uint8_t* scratch,
                           const ChunkBlob* blobs, size_t n_blobs, ui::MapView::TimeUsFn time_us) {
  // Everything is placed in the caller's memory (PSRAM on the device), nothing on the stack:
  // the store's table, the renderer's scratch and the paths first, then the chunk arena.
  size_t at = 0;
  const auto place = [&](size_t bytes) {
    uint8_t* where = arena + at;  // NOLINT(*-pointer-arithmetic)
    at += (bytes + 15) / 16 * 16;
    return where;
  };
  uint8_t* store_mem = place(sizeof(core::ChunkStore));
  renderer_ = new (place(sizeof(core::MapRenderer))) core::MapRenderer();
  highway_ = new (place(sizeof(Path))) Path();
  ghat_ = new (place(sizeof(Path))) Path();
  store_ = new (store_mem) core::ChunkStore(arena + at, arena_bytes - at);  // NOLINT

  (void)LoadChunks(*store_, scratch, blobs, n_blobs);
  FindMotorway(1, *highway_);
  FindMotorway(0, *ghat_);

  lv_obj_t* screen = lv_obj_create(nullptr);
  lv_obj_remove_style_all(screen);
  lv_obj_add_style(screen, ui::theme::ScreenStyle(), 0);
  lv_obj_remove_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
  view_.Create(screen, 0, 0, 480, 480, store_, renderer_, time_us);
  // A label over the map: overlays must still draw on top.
  label_ = lv_label_create(screen);
  lv_obj_add_style(label_, ui::theme::TextStyle(ui::theme::Text::kStoppedOnly), 0);
  lv_obj_add_style(label_, ui::theme::CardStyle(), 0);
  lv_obj_align(label_, LV_ALIGN_TOP_MID, 0, 70);
  // A rider chevron at the anchor.
  lv_obj_t* rider = lv_obj_create(screen);
  lv_obj_remove_style_all(rider);
  lv_obj_set_size(rider, 18, 18);
  lv_obj_set_style_radius(rider, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_opa(rider, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(rider, ui::theme::Color(0xFFFFFF), 0);
  lv_obj_set_pos(rider, 240 - 9, 320 - 9);
  lv_screen_load(screen);
  Place(0, 0);
  return store_->Count();
}

bool MapBenchScene::FindMotorway(uint8_t level, Path& out) const {
  // The longest motorway line at this level, in ride-frame metres.
  out.n = 0;
  size_t best_points = 0;
  for (size_t i = 0; i < store_->Count(); ++i) {
    const core::StoredChunk c = store_->At(i);
    if (c.id.level != level) {
      continue;
    }
    for (uint16_t s = 0; s < c.n_lines; ++s) {
      const core::MapShape& line = c.lines[s];  // NOLINT(*-pointer-arithmetic)
      if (line.kind != 0 || line.count <= best_points) {
        continue;
      }
      best_points = line.count;
      const float q = static_cast<float>(c.quantum_dm) / 10.0F;
      out.n = 0;
      for (uint32_t k = line.first; k < uint32_t{line.first} + line.count && out.n < Path::kMax;
           ++k) {
        const core::MapPoint& p = c.points[k];  // NOLINT(*-pointer-arithmetic)
        out.x[out.n] = static_cast<float>(c.origin_x_m) + static_cast<float>(p.x) * q;
        out.y[out.n] = static_cast<float>(c.origin_y_m) + static_cast<float>(p.y) * q;
        out.along[out.n] = out.n == 0
                               ? 0
                               : out.along[out.n - 1] + std::hypot(out.x[out.n] - out.x[out.n - 1],
                                                                   out.y[out.n] - out.y[out.n - 1]);
        ++out.n;
      }
    }
  }
  return out.n >= 2;
}

void MapBenchScene::PointAt(const Path& path, float d, float* x, float* y) {
  d = std::max(0.0F, std::min(d, path.along[path.n - 1]));
  size_t k = 1;
  while (k + 1 < path.n && path.along[k] < d) {
    ++k;
  }
  const float seg = path.along[k] - path.along[k - 1];
  const float f = seg > 0 ? (d - path.along[k - 1]) / seg : 0;
  *x = path.x[k - 1] + (path.x[k] - path.x[k - 1]) * f;
  *y = path.y[k - 1] + (path.y[k] - path.y[k - 1]) * f;
}

void MapBenchScene::Place(size_t phase, uint32_t t_ms) {
  const Phase& p = kPhases[phase];  // NOLINT(*-constant-array-index)
  const float t = static_cast<float>(t_ms) / 1000.0F;
  core::MapCamera cam;
  cam.metres_per_px = p.metres_per_px;
  if (p.city) {
    const float a = p.speed_ms * t / kCityR;
    cam.x_m = kCityX + kCityR * std::sin(a);
    cam.y_m = kCityY + kCityR * std::cos(a);
    // Heading along the orbit, swinging +-25 degrees: the map keeps rotating (ADR-0009 spike).
    cam.heading_deg = a * kRadToDeg + 90.0F + 25.0F * std::sin(t * 0.8F);
  } else {
    const Path& path = p.on_ghat ? *ghat_ : *highway_;
    if (path.n < 2) {
      return;
    }
    const float total = path.along[path.n - 1];
    const float d = std::fmod(p.speed_ms * t, total);
    float x = 0;
    float y = 0;
    PointAt(path, d, &x, &y);
    cam.x_m = x;
    cam.y_m = y;
    // Heading from 25 m behind to 25 m ahead: it turns smoothly through bends, as the real map's
    // smoothed GPS bearing will (G4), instead of jumping at every vertex.
    constexpr float kLookM = 25;
    float bx = 0;
    float by = 0;
    float ax = 0;
    float ay = 0;
    PointAt(path, d - kLookM, &bx, &by);
    PointAt(path, d + kLookM, &ax, &ay);
    cam.heading_deg = std::atan2(ax - bx, ay - by) * kRadToDeg;
  }
  view_.Set(cam, p.level, StyleFor(p));
  lv_label_set_text_static(label_, p.name);
}

void MapBenchScene::Step(uint32_t now_ms) {
  phase_ = (now_ms / kPhaseMs) % kNumPhases;
  Place(phase_, now_ms % kPhaseMs);
}

void MapBenchScene::Show(size_t phase, uint32_t ms_into_phase) {
  phase_ = phase % kNumPhases;
  Place(phase_, ms_into_phase);
}

const char* MapBenchScene::PhaseName() const { return kPhases[phase_].name; }  // NOLINT

size_t MapBenchScene::PhaseCount() const { return kNumPhases; }

}  // namespace sarathi::bench
