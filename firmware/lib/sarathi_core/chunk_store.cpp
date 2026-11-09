#include "chunk_store.h"

#include <algorithm>
#include <cstring>

namespace sarathi::core {

namespace {

constexpr size_t kAlign = alignof(MapShape) > alignof(MapPoint) ? alignof(MapShape)
                                                                : alignof(MapPoint);

size_t RoundUp(size_t n) { return (n + kAlign - 1) / kAlign * kAlign; }

bool Overlaps(const MetreBox& box, const ProtectedArea& area) {
  // Distance from the circle's centre to the box, squared, in 64 bits.
  const int64_t dx =
      std::max<int64_t>({0, int64_t{box.min_x} - area.x_m, int64_t{area.x_m} - box.max_x});
  const int64_t dy =
      std::max<int64_t>({0, int64_t{box.min_y} - area.y_m, int64_t{area.y_m} - box.max_y});
  return dx * dx + dy * dy <= int64_t{area.radius_m} * area.radius_m;
}

}  // namespace

size_t ChunkStore::BytesFor(uint32_t n_shapes, uint32_t n_points) {
  return RoundUp(n_shapes * sizeof(MapShape)) + RoundUp(n_points * sizeof(MapPoint));
}

MapShape* ChunkStore::Shapes(const Entry& e) const {
  return reinterpret_cast<MapShape*>(arena_ + e.offset);  // NOLINT(*-reinterpret-cast)
}

MapPoint* ChunkStore::Points(const Entry& e) const {
  const uint32_t n_shapes = uint32_t{e.header.n_lines} + e.header.n_polys;
  return reinterpret_cast<MapPoint*>(  // NOLINT(*-reinterpret-cast)
      arena_ + e.offset + RoundUp(n_shapes * sizeof(MapShape)));
}

bool ChunkStore::Protected(const Entry& e, const ProtectedArea* keep, size_t n_keep) const {
  for (size_t i = 0; i < n_keep; ++i) {
    if (Overlaps(e.box, keep[i])) return true;  // NOLINT(*-pointer-arithmetic)
  }
  return false;
}

bool ChunkStore::Begin(const ChunkHeader& header, const ProtectedArea* keep, size_t n_keep,
                       StoreResult* result) {
  Abort();
  const uint32_t n_shapes = uint32_t{header.n_lines} + header.n_polys;
  const bool fits_int16 = int32_t{header.extent} + kOverscan <= INT16_MAX;
  if (!fits_int16 || header.n_points > UINT16_MAX + 1U || header.quantum_dm == 0) {
    *result = StoreResult::kInvalid;
    return false;
  }
  for (size_t i = 0; i < count_; ++i) {
    if (SameTile(entries_[i].header.id, header.id)) {
      Erase(i, false);  // replaced, not evicted: the phone sent it
      break;
    }
  }
  const size_t need = BytesFor(n_shapes, header.n_points);
  if (need > capacity_) {
    *result = StoreResult::kFull;
    return false;
  }
  while (used_ + need > capacity_ || count_ == kMaxChunks) {
    size_t victim = count_;
    for (size_t i = 0; i < count_; ++i) {
      if (Protected(entries_[i], keep, n_keep)) continue;
      if (victim == count_ || entries_[i].last_drawn < entries_[victim].last_drawn) victim = i;
    }
    if (victim == count_) {
      *result = StoreResult::kFull;
      return false;
    }
    Erase(victim, true);
  }
  Entry& e = entries_[count_];
  e = Entry{};
  e.header = header;
  e.offset = static_cast<uint32_t>(used_);
  e.bytes = static_cast<uint32_t>(need);
  // Shapes start unset: count 0 marks them for Commit() to refuse.
  std::memset(arena_ + e.offset, 0, RoundUp(n_shapes * sizeof(MapShape)));
  open_ = true;
  *result = StoreResult::kStored;
  return true;
}

bool ChunkStore::SetShape(uint16_t index, uint8_t kind, uint32_t first, uint32_t count) {
  if (!open_) return false;
  const Entry& e = entries_[count_];
  const uint32_t n_shapes = uint32_t{e.header.n_lines} + e.header.n_polys;
  const uint32_t min_count = index < e.header.n_lines ? 2 : 3;
  if (index >= n_shapes || count < min_count || first + count > e.header.n_points) return false;
  MapShape& s = Shapes(e)[index];  // NOLINT(*-pointer-arithmetic)
  s.first = static_cast<uint16_t>(first);
  s.count = static_cast<uint16_t>(count);
  s.kind = kind;
  return true;
}

bool ChunkStore::SetPoint(uint32_t index, int32_t x, int32_t y) {
  if (!open_) return false;
  const Entry& e = entries_[count_];
  const int32_t hi = int32_t{e.header.extent} + kOverscan;
  if (index >= e.header.n_points || x < -kOverscan || y < -kOverscan || x > hi || y > hi) {
    return false;
  }
  Points(e)[index] = MapPoint{static_cast<int16_t>(x), static_cast<int16_t>(y)};  // NOLINT
  return true;
}

StoreResult ChunkStore::Commit() {
  if (!open_) return StoreResult::kInvalid;
  open_ = false;
  Entry& e = entries_[count_];
  const uint32_t n_shapes = uint32_t{e.header.n_lines} + e.header.n_polys;
  MapShape* shapes = Shapes(e);
  const MapPoint* points = Points(e);
  int32_t lo_x = INT16_MAX;
  int32_t lo_y = INT16_MAX;
  int32_t hi_x = INT16_MIN;
  int32_t hi_y = INT16_MIN;
  for (uint32_t i = 0; i < n_shapes; ++i) {
    MapShape& s = shapes[i];                         // NOLINT(*-pointer-arithmetic)
    if (s.count == 0) return StoreResult::kInvalid;  // never set
    MapShape box = s;
    box.min_x = box.min_y = INT16_MAX;
    box.max_x = box.max_y = INT16_MIN;
    for (uint32_t k = s.first; k < uint32_t{s.first} + s.count; ++k) {
      const MapPoint& p = points[k];  // NOLINT(*-pointer-arithmetic)
      box.min_x = std::min(box.min_x, p.x);
      box.min_y = std::min(box.min_y, p.y);
      box.max_x = std::max(box.max_x, p.x);
      box.max_y = std::max(box.max_y, p.y);
    }
    s = box;
    lo_x = std::min<int32_t>(lo_x, box.min_x);
    lo_y = std::min<int32_t>(lo_y, box.min_y);
    hi_x = std::max<int32_t>(hi_x, box.max_x);
    hi_y = std::max<int32_t>(hi_y, box.max_y);
  }
  if (n_shapes == 0) lo_x = lo_y = hi_x = hi_y = 0;
  const int32_t q_dm = e.header.quantum_dm;
  // Quanta to metres, rounded outwards.
  e.box.min_x = e.header.origin_x_m + (lo_x * q_dm - 9) / 10;
  e.box.min_y = e.header.origin_y_m + (lo_y * q_dm - 9) / 10;
  e.box.max_x = e.header.origin_x_m + (hi_x * q_dm + 9) / 10;
  e.box.max_y = e.header.origin_y_m + (hi_y * q_dm + 9) / 10;
  used_ += e.bytes;
  ++count_;
  return StoreResult::kStored;
}

void ChunkStore::Abort() { open_ = false; }

void ChunkStore::Clear() {
  open_ = false;
  count_ = 0;
  used_ = 0;
  n_evicted_ = 0;
}

void ChunkStore::Erase(size_t index, bool evicted) {
  const Entry gone = entries_[index];
  if (evicted && n_evicted_ < kMaxEvicted) evicted_[n_evicted_++] = gone.header.id;
  const size_t tail = gone.offset + gone.bytes;
  std::memmove(arena_ + gone.offset, arena_ + tail, used_ - tail);
  used_ -= gone.bytes;
  for (size_t i = index + 1; i < count_; ++i) {
    entries_[i - 1] = entries_[i];
    entries_[i - 1].offset -= gone.bytes;
  }
  --count_;
}

StoredChunk ChunkStore::At(size_t index) const {
  const Entry& e = entries_[index];
  StoredChunk c;
  c.id = e.header.id;
  c.origin_x_m = e.header.origin_x_m;
  c.origin_y_m = e.header.origin_y_m;
  c.quantum_dm = e.header.quantum_dm;
  c.box = e.box;
  c.lines = Shapes(e);
  c.n_lines = e.header.n_lines;
  c.polys = Shapes(e) + e.header.n_lines;  // NOLINT(*-pointer-arithmetic)
  c.n_polys = e.header.n_polys;
  c.points = Points(e);
  c.n_points = e.header.n_points;
  return c;
}

bool ChunkStore::Contains(const ChunkId& id) const {
  for (size_t i = 0; i < count_; ++i) {
    const ChunkId& have = entries_[i].header.id;
    if (SameTile(have, id) && have.data_version == id.data_version) return true;
  }
  return false;
}

size_t ChunkStore::TakeEvicted(ChunkId* out, size_t capacity) {
  const size_t n = std::min(capacity, n_evicted_);
  std::copy(evicted_, evicted_ + n, out);  // NOLINT(*-pointer-arithmetic)
  // Anything not taken stays for the next call.
  std::copy(evicted_ + n, evicted_ + n_evicted_, evicted_);  // NOLINT(*-pointer-arithmetic)
  n_evicted_ -= n;
  return n;
}

}  // namespace sarathi::core
