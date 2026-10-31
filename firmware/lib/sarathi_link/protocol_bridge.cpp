#include "protocol_bridge.h"

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
      tracker_(storage.tracker) {}

void ProtocolBridge::Begin(core::AppModel& model) {
  model.SetCapabilities(kPhoneCapabilities);
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
        } else if constexpr (std::is_same_v<T, proto::Nav>) {
          OnNav(m, now_ms, model);
        } else if constexpr (std::is_same_v<T, proto::NavEnd>) {
          model.EndNav();
          ForgetRoute(model);
        } else if constexpr (std::is_same_v<T, proto::Call>) {
          decltype(core::CallState::caller) caller;
          AssignText(caller, m.caller.View());
          model.SetCall(Phase(m.phase), caller.View(), now_ms);
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
  reply.chunk_kb = 0;  // no map chunks until the on-device map (ADR-0009)
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
}

void ProtocolBridge::ForgetRoute(core::AppModel& model) {
  has_route_ = false;
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
      } else if (e == proto::BinError::kVerUnknown) {
        ++stats_.ignored;
      } else {
        DropMessage(message.type, proto::BinErrorName(e));
      }
      return;
    }
    default:
      // traffic and chunk wait for the on-device map (ADR-0009); hello said chunk_kb 0.
      ++stats_.ignored;
      return;
  }
}

void ProtocolBridge::OnPose(const proto::Pose& pose, uint32_t now_ms, core::AppModel& model) {
  // cm/s to km/h, rounded: 1 km/h = 27.78 cm/s.
  const uint32_t kmh = ((static_cast<uint32_t>(pose.speed_cms) * 36U) + 500U) / 1000U;
  model.SetSpeed(static_cast<uint16_t>(kmh));
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
