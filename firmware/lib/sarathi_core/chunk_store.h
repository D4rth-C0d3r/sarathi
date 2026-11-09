// Map chunk store for the on-device map (ADR-0009 "Device: chunk cache"): decoded chunks in one
// arena allocated at init, compacted on eviction. Points are kept as int16 quanta from the
// chunk's origin, and every line and polygon has a bounding box, so the renderer can cull
// without touching points. Eviction is least recently drawn first, but never a chunk within the
// protected areas (1 km of the rider and of the route ahead). Pure logic.

#pragma once

#include <cstddef>
#include <cstdint>

namespace sarathi::core {

struct ChunkId {
  uint8_t level = 0;
  uint32_t tile_x = 0;
  uint32_t tile_y = 0;
  uint32_t data_version = 0;
};

[[nodiscard]] inline bool SameTile(const ChunkId& a, const ChunkId& b) {
  return a.level == b.level && a.tile_x == b.tile_x && a.tile_y == b.tile_y;
}

struct MapPoint {
  int16_t x = 0;
  int16_t y = 0;
};

// A road line (kind = class 0 motorway ... 6 service) or a polygon (kind 0 water, 1 park):
// `count` points from `first` in the chunk's points. Box in quanta.
struct MapShape {
  int16_t min_x = 0;
  int16_t min_y = 0;
  int16_t max_x = 0;
  int16_t max_y = 0;
  uint16_t first = 0;
  uint16_t count = 0;
  uint8_t kind = 0;
};

struct MetreBox {
  int32_t min_x = 0;
  int32_t min_y = 0;
  int32_t max_x = 0;
  int32_t max_y = 0;
};

// One stored chunk, as the renderer sees it.
struct StoredChunk {
  ChunkId id;
  int32_t origin_x_m = 0;
  int32_t origin_y_m = 0;
  uint8_t quantum_dm = 20;
  MetreBox box;  // the points' bounds in the ride frame
  const MapShape* lines = nullptr;
  uint16_t n_lines = 0;
  const MapShape* polys = nullptr;
  uint16_t n_polys = 0;
  const MapPoint* points = nullptr;
  uint32_t n_points = 0;
};

// Circles (ride frame, metres) whose chunks are never evicted.
struct ProtectedArea {
  int32_t x_m = 0;
  int32_t y_m = 0;
  int32_t radius_m = 0;
};

struct ChunkHeader {
  ChunkId id;
  int32_t origin_x_m = 0;
  int32_t origin_y_m = 0;
  uint8_t quantum_dm = 20;
  uint16_t extent = 0;
  uint16_t n_lines = 0;
  uint16_t n_polys = 0;
  uint32_t n_points = 0;
};

enum class StoreResult : uint8_t { kStored, kFull, kInvalid };

class ChunkStore {
 public:
  static constexpr size_t kMaxChunks = 256;
  static constexpr int32_t kOverscan = 64;  // protocol: points may stray this far outside

  ChunkStore(uint8_t* arena, size_t capacity) : arena_(arena), capacity_(capacity) {}

  // Starting a chunk: makes room (replacing the same tile, then evicting least recently drawn
  // chunks outside `keep`) and returns false with `result` set if it can't. Then the caller sets
  // every shape and point and calls Commit(). Begin() again or Abort() drops an uncommitted one.
  [[nodiscard]] bool Begin(const ChunkHeader& header, const ProtectedArea* keep, size_t n_keep,
                           StoreResult* result);
  // Shapes 0..n_lines-1 are lines, then the polygons. Returns false if it breaks the header.
  [[nodiscard]] bool SetShape(uint16_t index, uint8_t kind, uint32_t first, uint32_t count);
  // Returns false if the point lies outside the chunk's extent plus overscan.
  [[nodiscard]] bool SetPoint(uint32_t index, int32_t x, int32_t y);
  // Checks every shape was set within the points; computes the boxes. kInvalid drops it.
  StoreResult Commit();
  void Abort();

  void Clear();  // a new ride frame
  [[nodiscard]] size_t Count() const { return count_; }
  [[nodiscard]] size_t UsedBytes() const { return used_; }
  [[nodiscard]] size_t Capacity() const { return capacity_; }
  [[nodiscard]] StoredChunk At(size_t index) const;
  [[nodiscard]] bool Contains(const ChunkId& id) const;  // same tile and data version
  void MarkDrawn(size_t index, uint32_t frame) { entries_[index].last_drawn = frame; }
  // Evicted chunks since the last call (for the phone's resync, protocol 1.2 `evicted`).
  size_t TakeEvicted(ChunkId* out, size_t capacity);

  // Bytes a chunk needs, for sizing.
  [[nodiscard]] static size_t BytesFor(uint32_t n_shapes, uint32_t n_points);

 private:
  struct Entry {
    ChunkHeader header;
    MetreBox box;
    uint32_t offset = 0;
    uint32_t bytes = 0;
    uint32_t last_drawn = 0;
  };

  [[nodiscard]] MapShape* Shapes(const Entry& e) const;
  [[nodiscard]] MapPoint* Points(const Entry& e) const;
  void Erase(size_t index, bool evicted);
  [[nodiscard]] bool Protected(const Entry& e, const ProtectedArea* keep, size_t n_keep) const;

  uint8_t* arena_;
  size_t capacity_;
  size_t used_ = 0;
  Entry entries_[kMaxChunks] = {};
  size_t count_ = 0;
  bool open_ = false;  // entries_[count_] is being filled
  static constexpr size_t kMaxEvicted = 64;
  ChunkId evicted_[kMaxEvicted] = {};
  size_t n_evicted_ = 0;
};

}  // namespace sarathi::core
