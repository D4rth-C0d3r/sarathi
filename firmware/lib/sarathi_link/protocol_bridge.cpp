#include "protocol_bridge.h"

#include <algorithm>
#include <array>
#include <string_view>
#include <type_traits>
#include <variant>

#include "version.h"

namespace sarathi::link {
namespace {

constexpr uint32_t kSecondsPerMinute = 60;
constexpr uint32_t kSecondsPerHour = 3600;
constexpr uint32_t kSecondsPerDay = 24U * kSecondsPerHour;
constexpr uint32_t kMsPerSecond = 1000;

// The phone's maneuvers on the device's glyphs: codes 0-11 are the same; 12-19 have no glyph
// of their own yet and use the fallback from protocol.md section 3.
core::ManeuverType Glyph(proto::Maneuver m) {
  using M = proto::Maneuver;
  using G = core::ManeuverType;
  switch (m) {
    case M::kKeepLeft:
    case M::kForkLeft:
    case M::kRampLeft:
      return G::kSlightLeft;
    case M::kKeepRight:
    case M::kForkRight:
    case M::kRampRight:
      return G::kSlightRight;
    case M::kMerge:
    case M::kFerry:
      return G::kStraight;
    default:
      return static_cast<G>(m);  // the same order up to kArrive (static_asserts below)
  }
}

static_assert(static_cast<int>(core::ManeuverType::kArrive) ==
              static_cast<int>(proto::Maneuver::kArrive));
static_assert(static_cast<int>(core::ManeuverType::kRoundabout) ==
              static_cast<int>(proto::Maneuver::kRoundabout));

// Phone text is already valid UTF-8 without NUL (the decoder checks); control characters
// become spaces so a label never breaks a line by surprise.
template <size_t N>
void AssignText(core::FixedString<N>& out, std::string_view in) {
  std::array<char, N> buffer{};
  size_t n = 0;
  constexpr unsigned char kFirstPrintable = 0x20;
  for (const char c : in) {
    if (n == N) {
      break;
    }
    buffer.at(n++) = static_cast<unsigned char>(c) < kFirstPrintable ? ' ' : c;
  }
  out.Assign(std::string_view(buffer.data(), n));
}

core::CallPhase Phase(proto::CallPhase p) {
  switch (p) {
    case proto::CallPhase::kRinging:
      return core::CallPhase::kRinging;
    case proto::CallPhase::kActive:
      return core::CallPhase::kActive;
    case proto::CallPhase::kIdle:
      break;
  }
  return core::CallPhase::kIdle;
}

proto::MapStyle StyleFor(core::ThemeId theme) {
  switch (theme) {
    case core::ThemeId::kFighterHud:
      return proto::MapStyle::kHud;
    case core::ThemeId::kRacing:
      return proto::MapStyle::kRacing;
    case core::ThemeId::kClassic:
      break;
  }
  return proto::MapStyle::kClassic;
}

uint32_t Digits(std::string_view s, size_t at) {
  return static_cast<uint32_t>(((s[at] - '0') * 10) + (s[at + 1] - '0'));
}

}  // namespace

ProtocolBridge::ProtocolBridge(const BridgeStorage& storage)
    : reassembler_(storage.reassembly, storage.reassembly_size),
      route_points_(storage.route_points),
      route_points_size_(storage.route_points_size),
      tracker_(storage.tracker),
      chunks_(storage.chunks),
      chunk_shapes_{storage.chunk_shapes, storage.chunk_shapes_size},
      chunk_points_{storage.chunk_points, storage.chunk_points_size},
      chunk_keys_{storage.chunk_keys, storage.chunk_keys_size},
      binary_out_{storage.binary_out, storage.binary_out_size} {}

void ProtocolBridge::Begin(core::AppModel& model) {
  model.SetCapabilities(kPhoneCapabilities);
  model.SetSpeedValid(false);  // until poses arrive
  OnLinkEvent(LinkEvent::kAdvertising, model);
}

void ProtocolBridge::OnLinkEvent(LinkEvent event, core::AppModel& model) {
  switch (event) {
    case LinkEvent::kStopped:
      Disconnected(model);
      model.SetLinkStatus(core::LinkStatus::kOff);
      return;
    case LinkEvent::kAdvertising:
      Disconnected(model);
      model.SetLinkStatus(core::LinkStatus::kWaiting);
      return;
    case LinkEvent::kConnected:
      if (!secured_) {
        model.SetLinkStatus(core::LinkStatus::kSecuring);
      }
      return;
    case LinkEvent::kSecured:
      secured_ = true;
      if (!hello_) {
        model.SetLinkStatus(core::LinkStatus::kWaitingForApp);
      }
      return;
  }
}

// A lost link never leaves stale data showing. The route and the ride frame stay: the phone
// skips resending a route the device still holds (hello.route).
void ProtocolBridge::Disconnected(core::AppModel& model) {
  model.SetRiderPose(core::RiderPose{});
  secured_ = false;
  hello_ = false;
  consumed_ = 0;
  flow_consumed_ = 0;
  reassembler_.Reset();
  model.SetPhoneConnected(false);
  model.EndNav();
  model.SetCall(core::CallPhase::kIdle, {}, 0);
  model.SetMedia(core::MediaState{});
  model.SetBackend(core::BackendState{});
  model.EndPairingRequest();
  motion_.Reset();
  model.SetMotion(0, false);
  model.SetSpeedValid(false);
}

void ProtocolBridge::OnFrame(const uint8_t* frame, size_t size, uint32_t now_ms,
                             core::AppModel& model, Outbox& out) {
  if (!secured_) {
    return;  // the transport already refuses these; belt and braces
  }
  ++stats_.frames;
  ++consumed_;
  const proto::FeedResult result = reassembler_.Feed(frame, size);
  for (size_t i = 0; i < result.drop_count; ++i) {
    ++stats_.frames_dropped;
    stats_.last_frame_drop = result.drops.at(i);
  }
  if (result.has_message) {
    OnMessage(result.message, now_ms, model, out);
  }
  if (hello_ && consumed_ - flow_consumed_ >= kFlowWindow / 2) {
    SendFlow(now_ms, out);
  }
}

void ProtocolBridge::OnMessage(const proto::Message& message, uint32_t now_ms,
                               core::AppModel& model, Outbox& out) {
  const bool hello = message.type == static_cast<uint8_t>(proto::MsgType::kHello);
  if (!hello && !hello_) {
    ++stats_.ignored;  // nothing but hello counts before it (and after a major mismatch)
    return;
  }
  if (message.type >= static_cast<uint8_t>(proto::MsgType::kPose)) {
    OnBinary(message, now_ms, model);
    return;
  }
  proto::PhoneMessage decoded;
  const proto::JsonError e = json_.Decode(message.type, message.payload, message.size, &decoded);
  if (e == proto::JsonError::kUnknownType) {
    ++stats_.ignored;
    return;
  }
  if (e != proto::JsonError::kNone) {
    DropMessage(message.type, proto::JsonErrorName(e));
    return;
  }
  OnJson(decoded, now_ms, model, out);
}

void ProtocolBridge::OnJson(const proto::PhoneMessage& message, uint32_t now_ms,
                            core::AppModel& model, Outbox& out) {
  std::visit(
      [&](const auto& m) {
        using T = std::decay_t<decltype(m)>;
        if constexpr (std::is_same_v<T, proto::HelloPhone>) {
          OnHello(m, model, out);
        } else if constexpr (std::is_same_v<T, proto::Time>) {
          const std::string_view local = m.local.View();  // YYYY-MM-DDTHH:MM:SS, checked
          clock_valid_ = true;
          clock_base_s_ = (Digits(local, 11) * kSecondsPerHour) +
                          (Digits(local, 14) * kSecondsPerMinute) + Digits(local, 17);
          clock_sync_ms_ = now_ms;
        } else if constexpr (std::is_same_v<T, proto::PhoneBatt>) {
          model.SetPhoneBattery(m.level, m.charging);
        } else if constexpr (std::is_same_v<T, proto::FrameEpoch>) {
          OnFrameEpoch(m.epoch, model);
        } else if constexpr (std::is_same_v<T, proto::Backend>) {
          const core::BackendState& before = model.State().backend;
          core::BackendState backend;
          backend.valid = true;
          AssignText(backend.name, m.name.View());
          AssignText(backend.attribution, m.attribution.View());
          backend.traffic = m.traffic;
          if (before.valid && before.name != backend.name) {
            backend.switched = true;  // shown for a moment (nav_status)
            backend.switched_ms = now_ms;
          } else {
            backend.switched = before.switched;  // a repeat doesn't cut the notice short
            backend.switched_ms = before.switched_ms;
          }
          model.SetBackend(backend);
        } else if constexpr (std::is_same_v<T, proto::Event>) {
          // The phone follows a fallback with `backend` and a reroute with `route` and `nav`;
          // nothing to show on its own.
          ++stats_.events;
        } else if constexpr (std::is_same_v<T, proto::Nav>) {
          OnNav(m, now_ms, model);
        } else if constexpr (std::is_same_v<T, proto::NavEnd>) {
          ++stats_.nav_ends;
          model.EndNav();
          ForgetRoute(model);
        } else if constexpr (std::is_same_v<T, proto::Call>) {
          decltype(core::CallState::caller) caller;
          AssignText(caller, m.caller.View());
          model.SetCall(Phase(m.phase), caller.View(), now_ms);
        } else if constexpr (std::is_same_v<T, proto::Lock>) {
          // Kept until the dash restarts; the phone resends it after every hello.
          motion_.Configure({m.enabled, m.move_kmh, m.move_s, m.stop_kmh, m.stop_s});
          model.SetLockEnabled(m.enabled);
        } else if constexpr (std::is_same_v<T, proto::Media>) {
          core::MediaState media;
          media.available = m.avail;
          media.playing = m.playing;
          AssignText(media.title, m.title.View());
          AssignText(media.artist, m.artist.View());
          media.position_s = m.pos_s;
          media.duration_s = m.dur_s;
          model.SetMedia(media);
        }
      },
      message);
}

void ProtocolBridge::OnHello(const proto::HelloPhone& hello, core::AppModel& model, Outbox& out) {
  proto::HelloDevice reply;
  reply.fw.Assign(core::kFirmwareVersion);
  reply.seq = core::kFirmwareSeq;
  reply.styles.at(0).Assign("classic");
  reply.styles.at(1).Assign("hud");
  reply.styles.at(2).Assign("racing");
  reply.style_count = 3;
  // The chunk cache's size; 0 means it takes no chunks (ADR-0009).
  reply.chunk_kb = chunks_ == nullptr ? 0 : static_cast<uint16_t>(chunks_->Capacity() / 1024);
  reply.max_msg = static_cast<uint16_t>(proto::kMaxMessage);
  reply.has_route = has_route_;
  reply.route_id = route_id_;
  reply.route_v = route_version_;
  reply.has_epoch = has_epoch_;
  reply.epoch = epoch_;
  SendJson(proto::MsgType::kHello, reply, out);

  if (hello.proto_major != proto::kProtoMajor) {
    hello_ = false;
    model.SetPhoneConnected(false);
    model.SetLinkStatus(hello.proto_major < proto::kProtoMajor ? core::LinkStatus::kUpdateApp
                                                               : core::LinkStatus::kUpdateFirmware);
    return;
  }
  hello_ = true;
  model.SetPhoneConnected(true);
  model.SetLinkStatus(core::LinkStatus::kReady);
  SendFlow(0, out);
  style_theme_ = model.State().settings.theme;
  SendStyle(style_theme_, out);
  SendChunkHave(out);  // after a reconnect the phone sends only what's missing
}

void ProtocolBridge::OnNav(const proto::Nav& nav, uint32_t now_ms, core::AppModel& model) {
  const proto::Maneuver m = proto::ManeuverFromName(nav.m.View());
  core::Maneuver next;
  next.type = Glyph(m);
  next.roundabout_exit = m == proto::Maneuver::kRoundabout ? nav.exit : 0;
  decltype(core::NavState::street) street;
  AssignText(street, nav.street.View());
  model.SetGuidance(next, nav.dist_m, street.View(), nav.rem_s);
  model.SetNavFlags((nav.flags & proto::Bit(proto::NavFlag::kRerouting)) != 0,
                    (nav.flags & proto::Bit(proto::NavFlag::kOffRoute)) != 0);
  model.SetGuidanceStale(false);
  nav_received_ms_ = now_ms;
}

// A new ride frame: positions in the old one mean nothing now (ADR-0009).
void ProtocolBridge::OnFrameEpoch(uint16_t epoch, core::AppModel& model) {
  if (has_epoch_ && epoch == epoch_) {
    return;
  }
  has_epoch_ = true;
  epoch_ = epoch;
  ForgetRoute(model);
  // A new frame: chunks in the old one are in the wrong place (protocol.md, re-anchor).
  if (chunks_ != nullptr) {
    chunks_->Clear();
  }
  n_acked_ = 0;
  n_refused_ = 0;
  has_rider_ = false;
  model.SetRiderPose(core::RiderPose{});  // positions in the old frame mean nothing now
}

void ProtocolBridge::ForgetRoute(core::AppModel& model) {
  has_route_ = false;
  model.ClearTraffic();
  window_due_ = true;
  tracker_.Clear();
  model.ClearRouteWindow();
}

void ProtocolBridge::OnBinary(const proto::Message& message, uint32_t now_ms,
                              core::AppModel& model) {
  switch (static_cast<proto::MsgType>(message.type)) {
    case proto::MsgType::kPose: {
      proto::Pose pose;
      const proto::BinError e = proto::DecodePose(message.payload, message.size, &pose);
      if (e == proto::BinError::kNone && (!has_epoch_ || pose.epoch != epoch_)) {
        DropMessage(message.type, "epoch");
      } else if (e == proto::BinError::kNone) {
        OnPose(pose, now_ms, model);
      } else if (e == proto::BinError::kVerUnknown) {
        ++stats_.ignored;
      } else {
        DropMessage(message.type, proto::BinErrorName(e));
      }
      return;
    }
    case proto::MsgType::kRoute: {
      proto::Route route;
      const proto::BinError e = proto::DecodeRoute(message.payload, message.size, &route,
                                                   {route_points_, route_points_size_},
                                                   {route_markers_.data(), route_markers_.size()});
      if (e == proto::BinError::kNone && (!has_epoch_ || route.epoch != epoch_)) {
        DropMessage(message.type, "epoch");
      } else if (e == proto::BinError::kNone) {
        OnRoute(route, model);
        ++stats_.routes;
        stats_.route_id = route.route_id;
        stats_.route_version = route.version;
        stats_.route_points = static_cast<uint16_t>(route.n_points);
        stats_.route_markers = static_cast<uint16_t>(route.n_markers);
      } else if (e == proto::BinError::kVerUnknown) {
        ++stats_.ignored;
      } else {
        DropMessage(message.type, proto::BinErrorName(e));
      }
      return;
    }
    case proto::MsgType::kChunk:
      OnChunk(message);
      return;
    case proto::MsgType::kTraffic:
      OnTraffic(message, now_ms, model);
      return;
    default:
      ++stats_.ignored;
      return;
  }
}

void ProtocolBridge::OnPose(const proto::Pose& pose, uint32_t now_ms, core::AppModel& model) {
  // cm/s to km/h, rounded: 1 km/h = 27.78 cm/s.
  const auto kmh =
      static_cast<uint16_t>(((static_cast<uint32_t>(pose.speed_cms) * 36U) + 500U) / 1000U);
  model.SetMotion(kmh, motion_.Update(kmh, now_ms));
  model.SetSpeedValid(true);
  // Chunks around the rider are never evicted.
  has_rider_ = true;
  rider_x_m_ = pose.x_dm / 10;
  rider_y_m_ = pose.y_dm / 10;
  // The map moves with every pose, between route windows. Below walking pace the phone's
  // heading is noise: keep the last good one.
  constexpr float kDmPerM = 10.0F;
  constexpr float kCdegPerDegF = 100.0F;
  constexpr float kCmPerM = 100.0F;
  if (pose.speed_cms >= kHeadingMinSpeedCms) {
    rider_heading_deg_ = static_cast<float>(pose.heading_cdeg) / kCdegPerDegF;
  }
  model.SetRiderPose(core::RiderPose{true, static_cast<float>(pose.x_dm) / kDmPerM,
                                     static_cast<float>(pose.y_dm) / kDmPerM, rider_heading_deg_,
                                     static_cast<float>(pose.speed_cms) / kCmPerM, now_ms});
  // Poses come five times a second; the map gets a new window at most once a second.
  if (!tracker_.HasRoute() || (!window_due_ && now_ms - window_ms_ < core::kWindowPeriodMs)) {
    return;
  }
  window_due_ = false;
  window_ms_ = now_ms;
  constexpr float kMetresPerDm = 0.1F;
  const core::MetricPoint rider{static_cast<float>(pose.x_dm) * kMetresPerDm,
                                static_cast<float>(pose.y_dm) * kMetresPerDm};
  const core::RouteFix fix = tracker_.Locate(rider);
  constexpr uint16_t kCdegPerDeg = 100;
  const uint16_t heading = pose.speed_cms >= kHeadingMinSpeedCms
                               ? static_cast<uint16_t>(pose.heading_cdeg / kCdegPerDeg)
                               : tracker_.HeadingAt(fix.along_m);
  core::RouteWindow window;
  tracker_.Window(rider, fix, heading, core::kWindowBehindM, core::kWindowAheadM,
                  core::kWindowToleranceM, window);
  window.sequence = ++window_sequence_;
  window.has_position = true;
  window.rider_x_m = rider.x;
  window.rider_y_m = rider.y;
  model.SetRouteWindow(window, now_ms);
}

// The route replaces the one held (a reroute has a new version). Positions arrive in quanta.
void ProtocolBridge::OnRoute(const proto::Route& route, core::AppModel& model) {
  constexpr float kMetresPerDm = 0.1F;
  if (!tracker_.SetRoute(route.points, route.n_points,
                         static_cast<float>(route.quantum_dm) * kMetresPerDm)) {
    ForgetRoute(model);
    DropMessage(static_cast<uint8_t>(proto::MsgType::kRoute), "no_room");
    return;
  }
  for (size_t i = 0; i < route.n_markers; ++i) {
    const proto::RouteMarker& marker = route.markers[i];  // NOLINT(*-pointer-arithmetic)
    const auto m = static_cast<proto::Maneuver>(
        marker.maneuver < proto::kManeuverCount ? marker.maneuver
                                                : static_cast<uint8_t>(proto::Maneuver::kStraight));
    core::Maneuver glyph;
    glyph.type = Glyph(m);
    glyph.roundabout_exit = m == proto::Maneuver::kRoundabout ? marker.exit : 0;
    tracker_.AddManeuver(marker.point, glyph);  // the decoder checked the order
  }
  // A new route or version: its traffic comes next; the old one doesn't fit it.
  if (!has_route_ || route.route_id != route_id_ || route.version != route_version_) {
    model.ClearTraffic();
  }
  has_route_ = true;
  window_due_ = true;
  route_id_ = route.route_id;
  route_version_ = route.version;
}

void ProtocolBridge::Tick(uint32_t now_ms, core::AppModel& model, Outbox& out) {
  const core::PairingState& pairing = model.State().pairing;
  if (pairing.requested && now_ms - pairing.since_ms >= kPhonePairingTimeoutMs) {
    model.EndPairingRequest();
  }
  if (clock_valid_) {
    const uint32_t seconds =
        (clock_base_s_ + ((now_ms - clock_sync_ms_) / kMsPerSecond)) % kSecondsPerDay;
    const auto minutes = static_cast<uint16_t>(seconds / kSecondsPerMinute);
    if (!model.State().clock.valid || model.State().clock.minutes_of_day != minutes) {
      model.SetClock(minutes);
    }
  }
  if (!hello_) {
    return;
  }
  if (model.State().ride.speed_valid && !motion_.Fresh(now_ms)) {
    motion_.Reset();  // no pose for 5 s: speed unknown, settings lock while navigating
    model.SetMotion(0, false);
    model.SetSpeedValid(false);
  }
  const core::NavState& nav = model.State().nav;
  if (nav.active && !nav.guidance_stale && now_ms - nav_received_ms_ >= kNavStaleMs) {
    model.SetGuidanceStale(true);
  }
  if (consumed_ != flow_consumed_ && now_ms - flow_sent_ms_ >= kFlowRefreshMs) {
    SendFlow(now_ms, out);
  }
  if (model.State().settings.theme != style_theme_) {
    style_theme_ = model.State().settings.theme;
    SendStyle(style_theme_, out);
  }
  if (now_ms - ack_sent_ms_ >= kChunkAckMs) {
    SendChunkAck(now_ms, out);
  }
}

// Traffic for the route being ridden (ADR-0009, step G5); for another route or version it's
// stale and ignored (protocol.md, traffic).
void ProtocolBridge::OnTraffic(const proto::Message& message, uint32_t now_ms,
                               core::AppModel& model) {
  ++stats_.traffic;
  stats_.traffic_bytes = static_cast<uint16_t>(message.size);
  proto::Traffic traffic;
  const proto::BinError e = proto::DecodeTraffic(
      message.payload, message.size, &traffic, {traffic_entries_.data(), traffic_entries_.size()});
  if (e == proto::BinError::kVerUnknown) {
    ++stats_.ignored;
    return;
  }
  if (e != proto::BinError::kNone) {
    DropMessage(message.type, proto::BinErrorName(e));
    return;
  }
  if (!has_route_ || traffic.route_id != route_id_ || traffic.version != route_version_) {
    DropMessage(message.type, "stale");
    return;
  }
  core::TrafficState state;
  state.valid = true;
  state.received_ms = now_ms == 0 ? 1 : now_ms;  // 0 reads as "never"
  state.count = static_cast<uint16_t>(std::min(traffic.n_entries, core::kMaxTrafficStretches));
  for (size_t i = 0; i < state.count; ++i) {
    const proto::TrafficEntry& t = traffic.entries[i];  // NOLINT(*-pointer-arithmetic)
    state.offset_m.at(i) = t.offset_m;
    state.length_m.at(i) = t.length_m;
    state.level.at(i) = static_cast<core::TrafficLevel>(std::min<uint8_t>(t.level, 3));
  }
  model.SetTraffic(state);
}

void ProtocolBridge::OnChunk(const proto::Message& message) {
  if (chunks_ == nullptr) {
    ++stats_.ignored;  // hello said chunk_kb 0
    return;
  }
  proto::Chunk chunk;
  const proto::BinError e =
      proto::DecodeChunk(message.payload, message.size, &chunk, chunk_shapes_, chunk_points_);
  if (e == proto::BinError::kVerUnknown) {
    ++stats_.ignored;
    return;
  }
  if (e != proto::BinError::kNone) {
    DropMessage(message.type, proto::BinErrorName(e));  // no key to refuse it by
    return;
  }
  const auto refuse = [&](proto::RefuseReason reason) {
    ++stats_.chunks_refused;
    if (n_refused_ < refused_.size()) {
      refused_.at(n_refused_++) = proto::RefusedChunk{chunk.key, reason};
    }
  };
  if (!has_epoch_ || chunk.epoch != epoch_) {
    refuse(proto::RefuseReason::kEpoch);
    return;
  }
  core::ChunkHeader h;
  h.id = core::ChunkId{chunk.key.level, chunk.key.tile_x, chunk.key.tile_y, chunk.key.data_version};
  h.origin_x_m = chunk.origin_x_m;
  h.origin_y_m = chunk.origin_y_m;
  h.quantum_dm = chunk.quantum_dm;
  h.extent = chunk.extent;
  h.n_lines = static_cast<uint16_t>(chunk.n_lines);
  h.n_polys = static_cast<uint16_t>(chunk.n_polys);
  h.n_points = static_cast<uint32_t>(chunk.n_points);
  const core::ProtectedArea keep{rider_x_m_, rider_y_m_, kChunkKeepM};
  core::StoreResult result{};
  if (!chunks_->Begin(h, &keep, has_rider_ ? 1 : 0, &result)) {
    refuse(result == core::StoreResult::kFull ? proto::RefuseReason::kFull
                                              : proto::RefuseReason::kInvalid);
    return;
  }
  bool ok = true;
  for (size_t i = 0; ok && i < chunk.n_lines + chunk.n_polys; ++i) {
    // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic)
    const proto::ChunkShape& s =
        i < chunk.n_lines ? chunk.lines[i] : chunk.polys[i - chunk.n_lines];
    ok = chunks_->SetShape(static_cast<uint16_t>(i), s.kind, s.first, s.count);
  }
  for (size_t k = 0; ok && k < chunk.n_points; ++k) {
    // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic)
    ok = chunks_->SetPoint(static_cast<uint32_t>(k), chunk.points[k].x, chunk.points[k].y);
  }
  if (!ok || chunks_->Commit() != core::StoreResult::kStored) {
    chunks_->Abort();
    refuse(proto::RefuseReason::kInvalid);
    return;
  }
  ++stats_.chunks_stored;
  stats_.chunk_bytes += static_cast<uint32_t>(message.size);
  if (n_acked_ < acked_.size()) {
    acked_.at(n_acked_++) = chunk.key;
  }
}

void ProtocolBridge::SendChunkAck(uint32_t now_ms, Outbox& out) {
  std::array<core::ChunkId, proto::kMaxAckEntries> gone{};
  const size_t n_gone = chunks_ == nullptr ? 0 : chunks_->TakeEvicted(gone.data(), gone.size());
  if (n_acked_ == 0 && n_refused_ == 0 && n_gone == 0) {
    return;
  }
  std::array<proto::ChunkKey, proto::kMaxAckEntries> evicted{};
  for (size_t i = 0; i < n_gone; ++i) {
    const core::ChunkId& id = gone.at(i);
    evicted.at(i) = proto::ChunkKey{id.level, id.tile_x, id.tile_y, id.data_version};
  }
  stats_.chunks_evicted += static_cast<uint32_t>(n_gone);
  SendJson(
      proto::MsgType::kChunkAck,
      proto::ChunkAck{acked_.data(), n_acked_, refused_.data(), n_refused_, evicted.data(), n_gone},
      out);
  n_acked_ = 0;
  n_refused_ = 0;
  ack_sent_ms_ = now_ms;
}

void ProtocolBridge::SendChunkHave(Outbox& out) {
  if (chunks_ == nullptr || chunks_->Count() == 0 || !has_epoch_) {
    return;
  }
  const size_t n = std::min(chunks_->Count(), chunk_keys_.capacity);
  for (size_t i = 0; i < n; ++i) {
    const core::ChunkId id = chunks_->At(i).id;
    // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic)
    chunk_keys_.data[i] = proto::ChunkKey{id.level, id.tile_x, id.tile_y, id.data_version};
  }
  const size_t size = proto::EncodeBinary(proto::ChunkHave{epoch_, chunk_keys_.data, n},
                                          binary_out_.data, binary_out_.capacity);
  if (size == 0 ||
      !out.Send(static_cast<uint8_t>(proto::MsgType::kChunkHave), binary_out_.data, size)) {
    ++stats_.send_failed;
  }
}

LinkAction ProtocolBridge::OnCommand(const core::Command& command, uint32_t now_ms,
                                     core::AppModel& model, Outbox& out) {
  switch (command.type) {
    case core::CommandType::kPairAccept:
      return LinkAction::kPairAccept;
    case core::CommandType::kPairReject:
      return LinkAction::kPairReject;
    case core::CommandType::kForgetPhones:
      return LinkAction::kForgetPhones;
    case core::CommandType::kCallDismiss:
      model.SetCall(core::CallPhase::kIdle, {}, now_ms);
      return LinkAction::kNone;
    default:
      break;
  }
  if (!hello_) {
    return LinkAction::kNone;
  }
  switch (command.type) {
    case core::CommandType::kCallAccept:
      SendJson(proto::MsgType::kCallAction, proto::CallAction{proto::CallChoice::kAccept}, out);
      break;
    case core::CommandType::kCallReject:
      SendJson(proto::MsgType::kCallAction, proto::CallAction{proto::CallChoice::kReject}, out);
      break;
    case core::CommandType::kMediaPlayPause:
      SendJson(proto::MsgType::kMediaCmd, proto::MediaCmd{proto::MediaKey::kPlayPause}, out);
      break;
    case core::CommandType::kMediaNext:
      SendJson(proto::MsgType::kMediaCmd, proto::MediaCmd{proto::MediaKey::kNext}, out);
      break;
    case core::CommandType::kMediaPrev:
      SendJson(proto::MsgType::kMediaCmd, proto::MediaCmd{proto::MediaKey::kPrev}, out);
      break;
    default:
      break;
  }
  return LinkAction::kNone;
}

void ProtocolBridge::OnPairingRequest(uint32_t code, uint32_t now_ms, core::AppModel& model) {
  model.SetPairingRequest(code, now_ms);
}

void ProtocolBridge::OnPairingEnded(core::AppModel& model) { model.EndPairingRequest(); }

void ProtocolBridge::OnPairedPhones(uint8_t count, core::AppModel& model) {
  model.SetPairedPhones(count);
}

void ProtocolBridge::SendFlow(uint32_t now_ms, Outbox& out) {
  SendJson(proto::MsgType::kFlow, proto::Flow{kFlowWindow, consumed_}, out);
  flow_consumed_ = consumed_;
  flow_sent_ms_ = now_ms;
}

void ProtocolBridge::SendStyle(core::ThemeId theme, Outbox& out) {
  SendJson(proto::MsgType::kStyle, proto::Style{StyleFor(theme), 0}, out);
}

template <typename M>
void ProtocolBridge::SendJson(proto::MsgType type, const M& message, Outbox& out) {
  const size_t n = proto::EncodeJson(message, json_out_.data(), json_out_.size());
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast): text sent as bytes
  if (n == 0 || !out.Send(static_cast<uint8_t>(type),
                          reinterpret_cast<const uint8_t*>(json_out_.data()), n)) {
    ++stats_.send_failed;
  }
}

void ProtocolBridge::DropMessage(uint8_t type, const char* reason) {
  ++stats_.messages_dropped;
  stats_.last_drop_type = type;
  stats_.last_drop_reason = reason;
}

}  // namespace sarathi::link
