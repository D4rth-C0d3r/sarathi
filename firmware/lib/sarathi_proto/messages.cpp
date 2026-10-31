#include "messages.h"

namespace sarathi::proto {
namespace {

constexpr std::array<std::string_view, kManeuverCount> kManeuverNames = {
    "none",  "depart",      "straight",   "slight_left", "left",       "sharp_left", "slight_right",
    "right", "sharp_right", "uturn",      "roundabout",  "arrive",     "keep_left",  "keep_right",
    "merge", "fork_left",   "fork_right", "ramp_left",   "ramp_right", "ferry",
};

}  // namespace

std::string_view MsgTypeName(uint8_t type) {
  switch (static_cast<MsgType>(type)) {
    case MsgType::kHello:
      return "hello";
    case MsgType::kTime:
      return "time";
    case MsgType::kPhoneBatt:
      return "phone_batt";
    case MsgType::kFrame:
      return "frame";
    case MsgType::kBackend:
      return "backend";
    case MsgType::kEvent:
      return "event";
    case MsgType::kNav:
      return "nav";
    case MsgType::kNavEnd:
      return "nav_end";
    case MsgType::kCall:
      return "call";
    case MsgType::kMedia:
      return "media";
    case MsgType::kLock:
      return "lock";
    case MsgType::kChunkAck:
      return "chunk_ack";
    case MsgType::kFlow:
      return "flow";
    case MsgType::kStyle:
      return "style";
    case MsgType::kCallAction:
      return "call_action";
    case MsgType::kMediaCmd:
      return "media_cmd";
    case MsgType::kPose:
      return "pose";
    case MsgType::kRoute:
      return "route";
    case MsgType::kTraffic:
      return "traffic";
    case MsgType::kChunk:
      return "chunk";
    case MsgType::kChunkHave:
      return "chunk_have";
  }
  return {};
}

std::string_view ManeuverName(Maneuver m) {
  const auto i = static_cast<size_t>(m);
  return i < kManeuverNames.size() ? kManeuverNames.at(i) : std::string_view{};
}

Maneuver ManeuverFromName(std::string_view name) {
  for (size_t i = 0; i < kManeuverNames.size(); ++i) {
    if (kManeuverNames.at(i) == name) {
      return static_cast<Maneuver>(i);
    }
  }
  return Maneuver::kStraight;
}

}  // namespace sarathi::proto
