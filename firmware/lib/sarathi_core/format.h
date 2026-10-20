// Text for the UI: distances, durations and maneuvers. Pure logic; metric units for now.

#pragma once

#include <cstddef>
#include <cstdint>

#include "app_state.h"

namespace sarathi::core {

// "0 m" ... "950 m" (rounded to 10 m), "1.2 km" ... "9.9 km", then "10 km", "123 km".
void FormatDistance(uint32_t metres, char* out, size_t size);

// "< 1 min", "4 min", "1 h 05".
void FormatDuration(uint32_t seconds, char* out, size_t size);

// "0:00" ... "59:59", "1:00:00": elapsed call time or track position.
void FormatClock(uint32_t seconds, char* out, size_t size);

// Short instruction for a maneuver, e.g. "Right", "Slight left", "Exit 2", "Arrive".
void FormatManeuver(const Maneuver& maneuver, char* out, size_t size);

}  // namespace sarathi::core
