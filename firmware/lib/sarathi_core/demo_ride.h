// Fake data source: replays a scripted demo ride (route, speed, stops, an incoming call and a
// playlist) so the UI can be developed and tested before the phone app exists. Deterministic
// in ride time. The script is in demo_ride_data.cpp; a real recorded ride can replace it once
// the Android app can capture one.

#pragma once

#include <cstddef>
#include <cstdint>

#include "data_source.h"
#include "route_geometry.h"

namespace sarathi::core {

// The scripted ride (defined in demo_ride_data.cpp).
struct DemoTrack {
  const char* title;
  const char* artist;
  uint32_t duration_s;
};

struct DemoStop {
  uint16_t vertex;
  uint32_t duration_ms;
};

struct DemoScript {
  const MetricPoint* points;
  size_t point_count;
  const RouteManeuver* maneuvers;
  const char* const* streets;  // street after each maneuver, same order
  size_t maneuver_count;
  const uint16_t* segment_speed_kmh;  // one per segment (point_count - 1)
  const DemoStop* stops;
  size_t stop_count;
  const DemoTrack* tracks;
  size_t track_count;
  const char* caller;
  uint32_t call_ring_at_ms;  // ride time
  uint32_t call_missed_after_ms;
  uint32_t call_ends_after_ms;  // once accepted
  uint32_t restart_after_arrival_ms;
};

const DemoScript& DefaultDemoScript();

// Route window settings (ADR-0002).
constexpr float kWindowBehindM = 100.0F;
constexpr float kWindowAheadM = 1500.0F;
constexpr float kWindowToleranceM = 3.0F;
constexpr uint32_t kWindowPeriodMs = 1000;

// The demo clock starts at 18:55, so auto day/night switches to night five minutes in.
constexpr uint32_t kDemoClockStartMs = ((18U * 60U) + 55U) * 60U * 1000U;

class DemoRide final : public DataSource {
 public:
  explicit DemoRide(const DemoScript& script = DefaultDemoScript());

  void Poll(uint32_t now_ms, AppModel& model) override;
  void OnCommand(const Command& command, uint32_t now_ms, AppModel& model) override;

  // Paused: ride time stops and the bike is stationary (unlocks settings).
  void SetPaused(bool paused) { paused_ = paused; }
  [[nodiscard]] bool Paused() const { return paused_; }

  // Replay speed, 1 = real time.
  void SetSpeedFactor(uint32_t factor) { speed_factor_ = factor == 0 ? 1 : factor; }

  [[nodiscard]] float PositionM() const { return position_m_; }
  [[nodiscard]] uint32_t RideTimeMs() const { return ride_ms_; }

 private:
  enum class CallStage : uint8_t { kWaiting, kRinging, kActive, kDone };

  void Restart();
  void Advance(uint32_t dt_ms);
  [[nodiscard]] uint16_t CurrentSpeedKmh() const;
  [[nodiscard]] const char* StreetFor(const RouteManeuver* maneuver) const;
  void PublishNav(uint32_t now_ms, AppModel& model);
  void PublishCall(uint32_t now_ms, AppModel& model);
  void PublishMedia(AppModel& model) const;
  void AdvanceMedia(uint32_t dt_ms);

  const DemoScript& script_;
  RoutePath path_;
  uint32_t speed_factor_ = 1;
  bool paused_ = false;
  bool started_ = false;
  uint32_t last_now_ms_ = 0;

  uint32_t ride_ms_ = 0;
  float position_m_ = 0.0F;
  size_t next_stop_ = 0;
  uint32_t stop_remaining_ms_ = 0;
  bool arrived_ = false;
  uint32_t arrived_at_ms_ = 0;
  bool window_due_ = true;
  uint32_t last_window_ms_ = 0;  // wall-clock time of the last window
  uint32_t window_sequence_ = 0;

  CallStage call_stage_ = CallStage::kWaiting;
  uint32_t call_stage_since_ms_ = 0;

  size_t track_ = 0;
  bool playing_ = true;
  uint32_t track_position_ms_ = 0;

  // Time of day, ms since midnight. Runs (at the replay speed) even while paused.
  uint32_t clock_ms_ = kDemoClockStartMs;
};

}  // namespace sarathi::core
