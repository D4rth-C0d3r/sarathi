// Our own phone link (protocol v1, docs/protocol/protocol.md, ADR-0007): turns frames from the
// phone into app state, and rider commands into messages for the phone. Pure C++ on top of
// sarathi_proto, so it runs in native tests; the BLE transport is sarathi_source.
//
// Threads: everything here runs on the UI task. The transport hands over each received frame
// and sends what the bridge puts in the Outbox.

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "app_model.h"
#include "binary_codec.h"
#include "frame.h"
#include "json_codec.h"
#include "route_tracker.h"

namespace sarathi::link {

constexpr core::Capabilities kPhoneCapabilities{
    true,  // call_accept
    true,  // call_reject
    true,  // media_control
    true,  // media_progress
    true,  // route_geometry
    true,  // speed (from pose)
    true,  // phone_pairing
};

// Frames the device can buffer (its receive queue holds more), and how long it waits before
// granting credit for fewer than half of them (protocol.md section 1).
constexpr uint8_t kFlowWindow = 32;
constexpr uint32_t kFlowRefreshMs = 1000;
// Guidance that stops arriving is shown as stale (protocol.md section 5).
constexpr uint32_t kNavStaleMs = 10000;
// The phone's pairing attempt times out on its own after this (Bluetooth SMP).
constexpr uint32_t kPhonePairingTimeoutMs = 30000;

// Where messages to the phone go. The transport fragments and notifies them later, so it
// copies the payload.
class Outbox {
 public:
  Outbox() = default;
  Outbox(const Outbox&) = delete;
  Outbox& operator=(const Outbox&) = delete;
  Outbox(Outbox&&) = delete;
  Outbox& operator=(Outbox&&) = delete;
  virtual ~Outbox() = default;

  // False if there's no room; the bridge counts it and goes on.
  virtual bool Send(uint8_t type, const uint8_t* payload, size_t size) = 0;
};

// What the transport reports about the BLE link.
enum class LinkEvent : uint8_t {
  kStopped,      // BLE paused for Wi-Fi
  kAdvertising,  // up, nobody connected (also after a disconnect)
  kConnected,    // a phone connected; the link isn't secured yet
  kSecured,      // encrypted, bonded and authenticated: messages may flow
};

// Rider commands the transport carries out itself (BLE pairing and bonds).
enum class LinkAction : uint8_t { kNone, kPairAccept, kPairReject, kForgetPhones };

// Caller-owned storage (PSRAM on the device): reassembly needs proto::kMaxMessage bytes, a
// route decodes into up to proto::kMaxRoutePoints points.
struct BridgeStorage {
  uint8_t* reassembly = nullptr;
  size_t reassembly_size = 0;
  proto::Point* route_points = nullptr;
  size_t route_points_size = 0;
  core::TrackerStorage tracker;  // as many points as route_points
};

// Below this speed (cm/s, about 3.6 km/h) the phone's heading is noise; the map follows the
// route's direction instead.
constexpr uint16_t kHeadingMinSpeedCms = 100;

// Counters for the transport's log lines.
struct BridgeStats {
  uint32_t frames = 0;
  uint32_t frames_dropped = 0;    // framing (proto::Drop)
  uint32_t messages_dropped = 0;  // decoding (JsonError, BinError) or the wrong epoch
  uint32_t ignored = 0;           // valid but not for now: before hello, unknown type, newer ver
  uint32_t send_failed = 0;
  proto::Drop last_frame_drop = proto::Drop::kShortFrame;
  uint8_t last_drop_type = 0;
  const char* last_drop_reason = "";
};

class ProtocolBridge {
 public:
  explicit ProtocolBridge(const BridgeStorage& storage);

  // Sets the capabilities; nothing is connected yet.
  void Begin(core::AppModel& model);

  // A link that isn't secured shows nothing from the phone; losing it clears live data.
  void OnLinkEvent(LinkEvent event, core::AppModel& model);

  // One frame from the phone (an rx write), only over a secured link.
  void OnFrame(const uint8_t* frame, size_t size, uint32_t now_ms, core::AppModel& model,
               Outbox& out);

  // Clock, staleness, flow credit and style changes; call every loop.
  void Tick(uint32_t now_ms, core::AppModel& model, Outbox& out);

  // A rider command: sent to the phone, or returned for the transport.
  LinkAction OnCommand(const core::Command& command, uint32_t now_ms, core::AppModel& model,
                       Outbox& out);

  // Pairing, from the BLE stack (same flow as the Chronos build).
  static void OnPairingRequest(uint32_t code, uint32_t now_ms, core::AppModel& model);
  static void OnPairingEnded(core::AppModel& model);
  static void OnPairedPhones(uint8_t count, core::AppModel& model);

  [[nodiscard]] const BridgeStats& Stats() const { return stats_; }

 private:
  void OnMessage(const proto::Message& message, uint32_t now_ms, core::AppModel& model,
                 Outbox& out);
  void OnJson(const proto::PhoneMessage& message, uint32_t now_ms, core::AppModel& model,
              Outbox& out);
  void OnBinary(const proto::Message& message, uint32_t now_ms, core::AppModel& model);
  void OnHello(const proto::HelloPhone& hello, core::AppModel& model, Outbox& out);
  void OnNav(const proto::Nav& nav, uint32_t now_ms, core::AppModel& model);
  void OnFrameEpoch(uint16_t epoch, core::AppModel& model);
  void OnPose(const proto::Pose& pose, uint32_t now_ms, core::AppModel& model);
  void OnRoute(const proto::Route& route, core::AppModel& model);
  void ForgetRoute(core::AppModel& model);
  void Disconnected(core::AppModel& model);
  void SendFlow(uint32_t now_ms, Outbox& out);
  void SendStyle(core::ThemeId theme, Outbox& out);
  template <typename M>
  void SendJson(proto::MsgType type, const M& message, Outbox& out);
  void DropMessage(uint8_t type, const char* reason);

  proto::Reassembler reassembler_;
  proto::PhoneJsonDecoder json_;
  proto::Point* route_points_;
  size_t route_points_size_;
  std::array<proto::RouteMarker, proto::kMaxRouteMarkers> route_markers_{};
  core::RouteTracker tracker_;
  uint32_t window_sequence_ = 0;
  std::array<char, proto::kMaxJson> json_out_{};

  bool secured_ = false;
  bool hello_ = false;     // the phone's hello, with our major version
  uint32_t consumed_ = 0;  // frames taken since the connection started
  uint32_t flow_consumed_ = 0;
  uint32_t flow_sent_ms_ = 0;
  core::ThemeId style_theme_ = core::ThemeId::kClassic;

  bool has_epoch_ = false;
  uint16_t epoch_ = 0;
  bool has_route_ = false;
  uint32_t route_id_ = 0;
  uint16_t route_version_ = 0;

  uint32_t nav_received_ms_ = 0;

  bool clock_valid_ = false;
  uint32_t clock_base_s_ = 0;
  uint32_t clock_sync_ms_ = 0;

  BridgeStats stats_;
};

}  // namespace sarathi::link
