// Map renderer bench (Phase 4 step G1, ADR-0009 "Spike"): the recorded chunk sets
// (docs/fixtures/map) loaded into a ChunkStore and drawn by MapView with a moving, rotating
// camera, redrawn every frame, through fixed phases. Platform-free (LVGL, core, proto), so the
// device bench (bench/device_main.cpp) and the simulator (--mapbench) share it. Not product code.

#pragma once

#include <cstddef>
#include <cstdint>

#include "chunk_store.h"
#include "map_render.h"
#include "map_view.h"

namespace sarathi::bench {

struct ChunkBlob {
  const uint8_t* data;
  size_t size;
};

class MapBenchScene {
 public:
  static constexpr uint32_t kPhaseMs = 30000;
  static constexpr size_t kScratchBytes = 128 * 1024;  // decoding one chunk

  // `arena` holds the store; `scratch` (kScratchBytes) decodes chunks. Builds the screen and
  // loads it. Returns the number of chunks stored.
  size_t Init(uint8_t* arena, size_t arena_bytes, uint8_t* scratch, const ChunkBlob* blobs,
              size_t n_blobs, ui::MapView::TimeUsFn time_us);

  // Moves the camera for `now_ms` and redraws. Phases run in a loop.
  void Step(uint32_t now_ms);
  // Jumps to a phase and time within it (for screenshots).
  void Show(size_t phase, uint32_t ms_into_phase);

  [[nodiscard]] const char* PhaseName() const;
  [[nodiscard]] size_t PhaseCount() const;
  ui::MapView::Stats TakeStats() { return view_.TakeStats(); }
  [[nodiscard]] const core::ChunkStore& Store() const { return *store_; }

 private:
  struct Path {
    static constexpr size_t kMax = 2048;
    float x[kMax];
    float y[kMax];
    float along[kMax];  // metres from the start
    size_t n = 0;
  };

  void Place(size_t phase, uint32_t t_ms);
  bool FindMotorway(uint8_t level, Path& out) const;

  core::ChunkStore* store_ = nullptr;
  core::MapRenderer* renderer_ = nullptr;
  ui::MapView view_;
  lv_obj_t* label_ = nullptr;
  size_t phase_ = 0;
  Path* highway_ = nullptr;  // L1 motorway
  Path* ghat_ = nullptr;     // L0 motorway
};

}  // namespace sarathi::bench
