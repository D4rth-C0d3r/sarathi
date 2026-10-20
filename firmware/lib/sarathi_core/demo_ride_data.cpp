// The scripted demo ride: about 2.9 km with four maneuvers and arrival, two stops (a traffic
// light and a roundabout give-way), an incoming call at 60 s and a three-track playlist.
// Coordinates are metres east/north of the start. Names are fictional.

#include <array>

#include "demo_ride.h"

namespace sarathi::core {
namespace {

constexpr std::array<MetricPoint, 10> kPoints = {{
    {0.0F, 0.0F},       // 0 start, heading north
    {0.0F, 400.0F},     // 1 traffic light, turn right
    {300.0F, 400.0F},   // 2
    {450.0F, 430.0F},   // 3 gentle bend left
    {600.0F, 500.0F},   // 4
    {700.0F, 600.0F},   // 5 turn left
    {700.0F, 1000.0F},  // 6 roundabout, exit 2 (straight on)
    {700.0F, 1400.0F},  // 7 slight right
    {900.0F, 1700.0F},  // 8
    {900.0F, 2300.0F},  // 9 destination
}};

constexpr std::array<RouteManeuver, 5> kManeuvers = {{
    {1, {ManeuverType::kRight, 0}},
    {5, {ManeuverType::kLeft, 0}},
    {6, {ManeuverType::kRoundabout, 2}},
    {7, {ManeuverType::kSlightRight, 0}},
    {9, {ManeuverType::kArrive, 0}},
}};

constexpr std::array<const char*, 5> kStreets = {{
    "Station Road",
    "Lake View Drive",
    "Hill Street",
    "Ring Road",
    "Destination",
}};

// km/h for each segment, starting at the vertex with the same index.
constexpr std::array<uint16_t, 9> kSegmentSpeedKmh = {{40, 30, 45, 45, 35, 40, 35, 55, 50}};

constexpr std::array<DemoStop, 2> kStops = {{
    {1, 10000},  // traffic light
    {6, 5000},   // roundabout give-way
}};

constexpr std::array<DemoTrack, 3> kTracks = {{
    {"Open Road", "Demo Artist", 95},
    {"Night Ride", "Demo Artist", 80},
    {"Long Way Home", "Demo Band", 110},
}};

static_assert(kStreets.size() == kManeuvers.size(), "one street per maneuver");
static_assert(kSegmentSpeedKmh.size() == kPoints.size() - 1, "one speed per segment");

constexpr DemoScript kScript = {
    kPoints.data(),  kPoints.size(),    kManeuvers.data(),
    kStreets.data(), kManeuvers.size(), kSegmentSpeedKmh.data(),
    kStops.data(),   kStops.size(),     kTracks.data(),
    kTracks.size(),  "Demo Caller",
    60000,  // ring at 60 s of ride time
    20000,  // missed after 20 s of ringing
    25000,  // an accepted call ends after 25 s
    10000,  // restart 10 s after arriving
};

}  // namespace

const DemoScript& DefaultDemoScript() { return kScript; }

}  // namespace sarathi::core
