// Message types and the content of the JSON messages (docs/protocol/protocol.md sections 2
// and 3, ADR-0007). Enumerations list their values in the schemas' enum order, which is also
// the canonical order of sets; a set is a bit mask with bit i for value i.

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

#include "fixed_string.h"

namespace sarathi::proto {

using core::FixedString;

enum class MsgType : uint8_t {
  kHello = 0x01,
  kTime = 0x02,
  kPhoneBatt = 0x03,
  kFrame = 0x04,
  kBackend = 0x05,
  kEvent = 0x06,
  kNav = 0x07,
  kNavEnd = 0x08,
  kCall = 0x09,
  kMedia = 0x0A,
  kChunkAck = 0x10,
  kFlow = 0x11,
  kStyle = 0x12,
  kCallAction = 0x13,
  kMediaCmd = 0x14,
  kPose = 0x40,
  kRoute = 0x41,
  kTraffic = 0x42,
  kChunk = 0x43,
  kChunkHave = 0x44,
};

// The message name ("hello", "nav", ...), or an empty view for an unknown type.
[[nodiscard]] std::string_view MsgTypeName(uint8_t type);

constexpr uint8_t kProtoMajor = 1;
constexpr uint8_t kProtoMinor = 0;
constexpr size_t kMaxJson = 2048;

enum class Routing : uint8_t { kGoogle, kMappls, kOsmOnline, kOsmOffline };
enum class Search : uint8_t { kGoogle, kMappls, kOsm };  // also event.backend
enum class EventKind : uint8_t { kFallback, kReroute, kCostWarning };
enum class FallbackReason : uint8_t { kQuota, kOffline, kError, kNoRoute, kAccount };
enum class NavEndReason : uint8_t { kArrived, kCancelled, kError };
enum class CallPhase : uint8_t { kRinging, kActive, kIdle };
enum class MapStyle : uint8_t { kClassic, kHud, kRacing, kMinimal };
enum class RefuseReason : uint8_t { kFull, kInvalid, kEpoch };
enum class CallChoice : uint8_t { kAccept, kReject };
enum class MediaKey : uint8_t { kPlayPause, kNext, kPrev };

// Set members.
enum class PhoneCap : uint8_t { kGoogle, kMappls, kOsm, kOffline, kTraffic, kCalls, kMedia };
enum class NavFlag : uint8_t { kOffRoute, kRerouting, kArrived };
enum class MapLayer : uint8_t { kWater, kParks };

template <typename E>
constexpr uint16_t Bit(E e) {
  return static_cast<uint16_t>(1U << static_cast<unsigned>(e));
}

// Maneuver codes (protocol.md "Maneuvers"), shared with the binary route message.
enum class Maneuver : uint8_t {
  kNone,
  kDepart,
  kStraight,
  kSlightLeft,
  kLeft,
  kSharpLeft,
  kSlightRight,
  kRight,
  kSharpRight,
  kUturn,
  kRoundabout,
  kArrive,
  kKeepLeft,
  kKeepRight,
  kMerge,
  kForkLeft,
  kForkRight,
  kRampLeft,
  kRampRight,
  kFerry,
};
constexpr size_t kManeuverCount = 20;

[[nodiscard]] std::string_view ManeuverName(Maneuver m);
// The maneuver an `m` string names; an unknown name (a newer minor version) is kStraight.
[[nodiscard]] Maneuver ManeuverFromName(std::string_view name);

// ---- Phone → device.

struct HelloPhone {
  uint8_t proto_major = kProtoMajor;
  uint8_t proto_minor = kProtoMinor;
  FixedString<32> app;
  uint16_t caps = 0;  // PhoneCap bits
};

struct Time {
  FixedString<19> local;  // YYYY-MM-DDTHH:MM:SS
  int16_t utc_offset_min = 0;
};

struct PhoneBatt {
  uint8_t level = 0;
  bool charging = false;
};

struct FrameEpoch {
  uint16_t epoch = 0;
};

struct Backend {
  Routing routing = Routing::kGoogle;
  Search search = Search::kGoogle;
  FixedString<24> name;
  FixedString<64> attribution;
  bool traffic = false;
  bool two_wheeler = false;
};

// Fields beyond `kind` are meaningful only for the kinds noted.
struct Event {
  EventKind kind = EventKind::kReroute;
  Routing from = Routing::kGoogle;                 // fallback
  Routing to = Routing::kGoogle;                   // fallback
  FallbackReason reason = FallbackReason::kError;  // fallback
  Search backend = Search::kGoogle;                // cost_warning
  uint8_t percent = 0;                             // cost_warning
};

struct Nav {
  FixedString<16> m;  // as sent, so an unknown name survives; see ManeuverFromName
  uint8_t exit = 0;
  uint32_t dist_m = 0;
  FixedString<48> street;
  uint32_t rem_s = 0;
  uint32_t rem_m = 0;
  uint16_t flags = 0;  // NavFlag bits
};

struct NavEnd {
  NavEndReason reason = NavEndReason::kArrived;
};

struct Call {
  CallPhase phase = CallPhase::kIdle;
  FixedString<32> caller;
};

struct Media {
  bool avail = false;
  bool playing = false;
  FixedString<48> title;
  FixedString<48> artist;
  uint32_t pos_s = 0;
  uint32_t dur_s = 0;
};

// ---- Device → phone.

constexpr size_t kMaxStyles = 8;
constexpr size_t kMaxAckEntries = 64;

struct HelloDevice {
  uint8_t proto_major = kProtoMajor;
  uint8_t proto_minor = kProtoMinor;
  FixedString<16> fw;
  uint32_t seq = 0;
  std::array<FixedString<16>, kMaxStyles> styles{};
  size_t style_count = 0;
  uint16_t chunk_kb = 0;
  uint16_t max_msg = 0;
  bool has_route = false;  // false writes "route": null
  uint32_t route_id = 0;
  uint16_t route_v = 0;
  bool has_epoch = false;  // false writes "epoch": null
  uint16_t epoch = 0;
};

struct ChunkKey {
  uint8_t level = 0;
  uint32_t tile_x = 0;
  uint32_t tile_y = 0;
  uint32_t data_version = 0;
};

struct RefusedChunk {
  ChunkKey key;
  RefuseReason reason = RefuseReason::kInvalid;
};

// Points at the caller's lists, at most kMaxAckEntries each.
struct ChunkAck {
  const ChunkKey* stored = nullptr;
  size_t stored_count = 0;
  const RefusedChunk* refused = nullptr;
  size_t refused_count = 0;
};

struct Flow {
  uint8_t window = 1;
  uint32_t consumed = 0;
};

struct Style {
  MapStyle map = MapStyle::kClassic;
  uint16_t layers = 0;  // MapLayer bits
};

struct CallAction {
  CallChoice action = CallChoice::kReject;
};

struct MediaCmd {
  MediaKey cmd = MediaKey::kPlayPause;
};

}  // namespace sarathi::proto
