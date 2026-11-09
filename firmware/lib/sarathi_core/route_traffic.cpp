#include "route_traffic.h"

#include <algorithm>

namespace sarathi::core {

namespace {

// The level at `along` and how far it holds (metres from the route's start).
TrafficLevel LevelAt(const TrafficState& t, uint32_t along, uint32_t* until) {
  for (uint16_t i = 0; i < t.count; ++i) {
    const uint32_t from = t.offset_m.at(i);
    const uint32_t to = from + t.length_m.at(i);
    if (along < from) {
      *until = from;  // a gap before this stretch: unknown
      return TrafficLevel::kUnknown;
    }
    if (along < to) {
      *until = to;
      return t.level.at(i);
    }
  }
  *until = UINT32_MAX;
  return TrafficLevel::kUnknown;
}

}  // namespace

size_t SplitRouteByTraffic(const RouteWindow& window, const TrafficState& traffic, uint32_t now_ms,
                           TrafficPiece* out, size_t capacity) {
  size_t n = 0;
  const bool stale = traffic.valid && now_ms - traffic.received_ms > kTrafficStaleMs;
  for (size_t i = 1; i < window.count && n < capacity; ++i) {
    const RoutePoint& a = window.points.at(i - 1);
    const RoutePoint& b = window.points.at(i);
    const uint32_t from = window.along_m.at(i - 1);
    const uint32_t to = window.along_m.at(i);
    if (!traffic.valid || stale || to <= from) {
      // NOLINTNEXTLINE(*-pointer-arithmetic)
      out[n++] = TrafficPiece{static_cast<float>(a.x_m), static_cast<float>(a.y_m),
                              static_cast<float>(b.x_m), static_cast<float>(b.y_m),
                              traffic.valid ? TrafficLevel::kUnknown : TrafficLevel::kFree};
      continue;
    }
    // Walk the segment's length, one piece per stretch it crosses.
    uint32_t at = from;
    while (at < to && n < capacity) {
      uint32_t until = 0;
      const TrafficLevel level = LevelAt(traffic, at, &until);
      const uint32_t end = std::min(until, to);
      const auto f0 = static_cast<float>(at - from) / static_cast<float>(to - from);
      const auto f1 = static_cast<float>(end - from) / static_cast<float>(to - from);
      const auto lerp = [](int16_t p, int16_t q, float f) {
        return static_cast<float>(p) + (static_cast<float>(q) - static_cast<float>(p)) * f;
      };
      // NOLINTNEXTLINE(*-pointer-arithmetic)
      out[n++] = TrafficPiece{lerp(a.x_m, b.x_m, f0), lerp(a.y_m, b.y_m, f0),
                              lerp(a.x_m, b.x_m, f1), lerp(a.y_m, b.y_m, f1), level};
      at = end;
    }
  }
  return n;
}

}  // namespace sarathi::core
