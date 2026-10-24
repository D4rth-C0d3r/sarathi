// NOLINTBEGIN(cppcoreguidelines-pro-bounds-pointer-arithmetic)
// The script is a set of caller-provided tables; bounds come from their counts.

#include "demo_ride.h"

#include <algorithm>
#include <cmath>

namespace sarathi::core {
namespace {

constexpr float kKmhToMps = 1000.0F / 3600.0F;
constexpr float kMsPerS = 1000.0F;
constexpr uint32_t kMaxStepMs = 1000;  // ignore longer gaps (e.g. a debugger pause)
constexpr float kEtaSpeedMps = 40.0F * kKmhToMps;

}  // namespace

DemoRide::DemoRide(const DemoScript& script)
    : script_(script),
      path_(script.points, script.point_count, script.maneuvers, script.maneuver_count) {}

void DemoRide::Restart() {
  ride_ms_ = 0;
  position_m_ = 0.0F;
  next_stop_ = 0;
  stop_remaining_ms_ = 0;
  arrived_ = false;
  window_due_ = true;
  call_stage_ = CallStage::kWaiting;
}

uint16_t DemoRide::CurrentSpeedKmh() const {
  if (paused_ || arrived_ || stop_remaining_ms_ > 0) {
    return 0;
  }
  size_t seg = 0;
  while (seg + 2 < script_.point_count && path_.VertexDistanceM(seg + 1) <= position_m_) {
    ++seg;
  }
  return script_.segment_speed_kmh[seg];
}

void DemoRide::Advance(uint32_t dt_ms) {
  ride_ms_ += dt_ms;
  if (arrived_) {
    if (ride_ms_ - arrived_at_ms_ >= script_.restart_after_arrival_ms) {
      Restart();
    }
    return;
  }
  if (stop_remaining_ms_ > 0) {
    const uint32_t wait = std::min(stop_remaining_ms_, dt_ms);
    stop_remaining_ms_ -= wait;
    dt_ms -= wait;
  }
  const float speed_mps = static_cast<float>(CurrentSpeedKmh()) * kKmhToMps;
  float next = position_m_ + (speed_mps * static_cast<float>(dt_ms) / kMsPerS);

  // Stop at the next scheduled stop if we reach it during this step.
  if (next_stop_ < script_.stop_count) {
    const float stop_at = path_.VertexDistanceM(script_.stops[next_stop_].vertex);
    if (position_m_ < stop_at && next >= stop_at) {
      next = stop_at;
      stop_remaining_ms_ = script_.stops[next_stop_].duration_ms;
      ++next_stop_;
    }
  }
  position_m_ = next;
  if (position_m_ >= path_.LengthM()) {
    position_m_ = path_.LengthM();
    arrived_ = true;
    arrived_at_ms_ = ride_ms_;
  }
}

const char* DemoRide::StreetFor(const RouteManeuver* maneuver) const {
  if (maneuver == nullptr) {
    return "";
  }
  const auto index = static_cast<size_t>(maneuver - script_.maneuvers);
  return index < script_.maneuver_count ? script_.streets[index] : "";
}

void DemoRide::PublishNav(uint32_t now_ms, AppModel& model) {
  const RouteManeuver* next = path_.NextManeuver(position_m_);
  if (next == nullptr) {  // at the destination
    model.SetGuidance(Maneuver{ManeuverType::kArrive, 0}, 0, "", 0);
  } else {
    const float remaining = path_.LengthM() - position_m_;
    model.SetGuidance(
        next->maneuver,
        static_cast<uint32_t>(std::lround(path_.VertexDistanceM(next->vertex) - position_m_)),
        StreetFor(next), static_cast<uint32_t>(std::lround(remaining / kEtaSpeedMps)));
  }

  // Like the phone, keep sending windows on wall-clock time, also while the bike stands still.
  if (window_due_ || now_ms - last_window_ms_ >= kWindowPeriodMs) {
    RouteWindow window{};
    path_.Window(position_m_, kWindowBehindM, kWindowAheadM, kWindowToleranceM, window);
    window.sequence = ++window_sequence_;
    model.SetRouteWindow(window, now_ms);
    last_window_ms_ = now_ms;
    window_due_ = false;
  }
}

void DemoRide::PublishCall(uint32_t now_ms, AppModel& model) {
  switch (call_stage_) {
    case CallStage::kWaiting:
      if (ride_ms_ >= script_.call_ring_at_ms) {
        call_stage_ = CallStage::kRinging;
        call_stage_since_ms_ = ride_ms_;
      }
      break;
    case CallStage::kRinging:
      if (ride_ms_ - call_stage_since_ms_ >= script_.call_missed_after_ms) {
        call_stage_ = CallStage::kDone;
      }
      break;
    case CallStage::kActive:
      if (ride_ms_ - call_stage_since_ms_ >= script_.call_ends_after_ms) {
        call_stage_ = CallStage::kDone;
      }
      break;
    case CallStage::kDone:
      break;
  }
  const CallPhase phase = call_stage_ == CallStage::kRinging  ? CallPhase::kRinging
                          : call_stage_ == CallStage::kActive ? CallPhase::kActive
                                                              : CallPhase::kIdle;
  model.SetCall(phase, script_.caller, now_ms);
}

void DemoRide::AdvanceMedia(uint32_t dt_ms) {
  if (!playing_ || script_.track_count == 0) {
    return;
  }
  track_position_ms_ += dt_ms;
  if (track_position_ms_ >= script_.tracks[track_].duration_s * 1000U) {
    track_ = (track_ + 1) % script_.track_count;
    track_position_ms_ = 0;
  }
}

void DemoRide::PublishMedia(AppModel& model) const {
  MediaState media{};
  if (script_.track_count > 0) {
    const DemoTrack& t = script_.tracks[track_];
    media.available = true;
    media.playing = playing_;
    media.title.Assign(t.title);
    media.artist.Assign(t.artist);
    media.position_s = track_position_ms_ / 1000U;
    media.duration_s = t.duration_s;
  }
  model.SetMedia(media);
}

void DemoRide::Poll(uint32_t now_ms, AppModel& model) {
  uint32_t dt_ms = 0;
  if (started_) {
    dt_ms = std::min(now_ms - last_now_ms_, kMaxStepMs) * speed_factor_;
  }
  started_ = true;
  last_now_ms_ = now_ms;

  if (!paused_) {
    Advance(dt_ms);
    AdvanceMedia(dt_ms);
  }
  constexpr uint32_t kDayMs = 24U * 60U * 60U * 1000U;
  clock_ms_ = (clock_ms_ + dt_ms) % kDayMs;
  model.SetClock(static_cast<uint16_t>(clock_ms_ / 60000U));
  model.SetSpeed(CurrentSpeedKmh());
  model.SetPhoneConnected(true);  // the demo stands in for a connected phone
  PublishNav(now_ms, model);
  PublishCall(now_ms, model);
  PublishMedia(model);
}

void DemoRide::OnCommand(const Command& command, uint32_t now_ms, AppModel& model) {
  switch (command.type) {
    case CommandType::kCallAccept:
      if (call_stage_ == CallStage::kRinging) {
        call_stage_ = CallStage::kActive;
        call_stage_since_ms_ = ride_ms_;
      }
      break;
    case CommandType::kCallReject:
      if (call_stage_ == CallStage::kRinging || call_stage_ == CallStage::kActive) {
        call_stage_ = CallStage::kDone;
      }
      break;
    case CommandType::kCallDismiss:
    case CommandType::kPairAccept:
    case CommandType::kPairReject:
    case CommandType::kForgetPhones:
      break;  // the demo has no BLE and supports reject, so the UI never offers these
    case CommandType::kMediaPlayPause:
      playing_ = !playing_;
      break;
    case CommandType::kMediaNext:
      if (script_.track_count > 0) {
        track_ = (track_ + 1) % script_.track_count;
        track_position_ms_ = 0;
      }
      break;
    case CommandType::kMediaPrev:
      if (script_.track_count > 0) {
        track_ = (track_ + script_.track_count - 1) % script_.track_count;
        track_position_ms_ = 0;
      }
      break;
  }
  PublishCall(now_ms, model);
  PublishMedia(model);
}

}  // namespace sarathi::core

// NOLINTEND(cppcoreguidelines-pro-bounds-pointer-arithmetic)
