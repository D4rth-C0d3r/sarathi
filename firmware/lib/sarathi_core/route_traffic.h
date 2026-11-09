// The route window split by traffic (ADR-0009 "Per-theme map styles", protocol `traffic`): one
// piece per run of a level, in metres relative to the window's rider. Pure logic.
// - No traffic for this route: the whole window is free flow (drawn as the plain route).
// - Traffic older than kTrafficStaleMs: all unknown, never silently old.
// - Otherwise by stretch; parts no stretch covers are unknown.

#pragma once

#include <cstddef>

#include "app_state.h"

namespace sarathi::core {

constexpr size_t kMaxTrafficPieces = 192;

struct TrafficPiece {
  float ax_m = 0;
  float ay_m = 0;
  float bx_m = 0;
  float by_m = 0;
  TrafficLevel level = TrafficLevel::kFree;
};

// Returns the number of pieces written (at most `capacity`).
size_t SplitRouteByTraffic(const RouteWindow& window, const TrafficState& traffic, uint32_t now_ms,
                           TrafficPiece* out, size_t capacity);

}  // namespace sarathi::core
