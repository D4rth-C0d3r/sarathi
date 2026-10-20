#include "format.h"

#include <cstdio>

namespace sarathi::core {
namespace {

constexpr uint32_t kMetresPerKm = 1000;
constexpr uint32_t kTenKm = 10000;
constexpr uint32_t kSecondsPerMinute = 60;
constexpr uint32_t kSecondsPerHour = 3600;

const char* ManeuverName(ManeuverType type) {
  switch (type) {
    case ManeuverType::kDepart:
      return "Depart";
    case ManeuverType::kStraight:
      return "Ahead";
    case ManeuverType::kSlightLeft:
      return "Slight left";
    case ManeuverType::kLeft:
      return "Left";
    case ManeuverType::kSharpLeft:
      return "Sharp left";
    case ManeuverType::kSlightRight:
      return "Slight right";
    case ManeuverType::kRight:
      return "Right";
    case ManeuverType::kSharpRight:
      return "Sharp right";
    case ManeuverType::kUTurn:
      return "U-turn";
    case ManeuverType::kRoundabout:
      return "Roundabout";
    case ManeuverType::kArrive:
      return "Arrive";
    case ManeuverType::kNone:
      break;
  }
  return "";
}

}  // namespace

void FormatDistance(uint32_t metres, char* out, size_t size) {
  if (metres < kMetresPerKm - 5) {
    const uint32_t rounded = (metres + 5) / 10 * 10;  // nearest 10 m
    std::snprintf(out, size, "%u m", static_cast<unsigned>(rounded));
  } else if (metres < kTenKm - 50) {
    const uint32_t tenths = (metres + 50) / 100;  // nearest 0.1 km
    std::snprintf(out, size, "%u.%u km", static_cast<unsigned>(tenths / 10),
                  static_cast<unsigned>(tenths % 10));
  } else {
    std::snprintf(out, size, "%u km",
                  static_cast<unsigned>((metres + (kMetresPerKm / 2)) / kMetresPerKm));
  }
}

void FormatDistance(uint32_t metres, Units units, char* out, size_t size) {
  if (units == Units::kMetric) {
    FormatDistance(metres, out, size);
    return;
  }
  constexpr double kFeetPerMetre = 3.28084;
  constexpr double kMetresPerMile = 1609.344;
  constexpr double kTenthMileM = kMetresPerMile / 10.0;
  const double m = static_cast<double>(metres);
  if (m < kTenthMileM - 0.5) {
    const auto feet = static_cast<unsigned>((m * kFeetPerMetre + 5.0) / 10.0) * 10U;
    std::snprintf(out, size, "%u ft", feet);
    return;
  }
  const double miles = m / kMetresPerMile;
  const auto tenths = static_cast<unsigned>(miles * 10.0 + 0.5);
  if (tenths < 100) {
    std::snprintf(out, size, "%u.%u mi", tenths / 10, tenths % 10);
  } else {
    std::snprintf(out, size, "%u mi", static_cast<unsigned>(miles + 0.5));
  }
}

void FormatDuration(uint32_t seconds, char* out, size_t size) {
  if (seconds < kSecondsPerMinute) {
    std::snprintf(out, size, "%s", "< 1 min");
    return;
  }
  const uint32_t minutes = (seconds + (kSecondsPerMinute / 2)) / kSecondsPerMinute;
  if (minutes < 60) {
    std::snprintf(out, size, "%u min", static_cast<unsigned>(minutes));
  } else {
    std::snprintf(out, size, "%u h %02u", static_cast<unsigned>(minutes / 60),
                  static_cast<unsigned>(minutes % 60));
  }
}

void FormatClock(uint32_t seconds, char* out, size_t size) {
  const auto h = static_cast<unsigned>(seconds / kSecondsPerHour);
  const auto m = static_cast<unsigned>((seconds % kSecondsPerHour) / kSecondsPerMinute);
  const auto s = static_cast<unsigned>(seconds % kSecondsPerMinute);
  if (h > 0) {
    std::snprintf(out, size, "%u:%02u:%02u", h, m, s);
  } else {
    std::snprintf(out, size, "%u:%02u", m, s);
  }
}

void FormatManeuver(const Maneuver& maneuver, char* out, size_t size) {
  if (maneuver.type == ManeuverType::kRoundabout && maneuver.roundabout_exit > 0) {
    std::snprintf(out, size, "Exit %u", static_cast<unsigned>(maneuver.roundabout_exit));
  } else {
    std::snprintf(out, size, "%s", ManeuverName(maneuver.type));
  }
}

}  // namespace sarathi::core
