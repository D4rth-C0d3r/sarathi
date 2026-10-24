// Turns decoded Chronos packets into app state, and rider commands into Chronos actions
// (ADR-0005). Pure C++: runs in native tests with recorded packets.
//
// What Chronos can't do shapes the capabilities: calls can only be dismissed on the dash (no
// accept or reject), there is no route geometry and no speed (so settings lock while
// navigating).

#pragma once

#include <array>
#include <cstdint>

#include "app_model.h"
#include "chronos_packets.h"

namespace sarathi::link {

constexpr core::Capabilities kChronosCapabilities{
    false,  // call_accept
    false,  // call_reject
    true,   // media_control
    false,  // media_progress
    false,  // route_geometry
    false,  // speed
};

enum class ChronosAction : uint8_t { kNone, kMusicToggle, kMusicNext, kMusicPrevious };

class ChronosBridge {
 public:
  // Sets the capabilities; the phone starts disconnected.
  void Begin(core::AppModel& model);

  void OnMessage(const ChronosMessage& message, uint32_t now_ms, core::AppModel& model);

  // A lost link ends navigation and the call screen: stale data is never left showing.
  void OnConnection(bool connected, core::AppModel& model);

  // Keeps the clock running between the phone's time syncs.
  void Tick(uint32_t now_ms, core::AppModel& model) const;

  // What to send to the phone for a rider command, if anything.
  ChronosAction OnCommand(const core::Command& command, uint32_t now_ms, core::AppModel& model);

 private:
  void OnNav(const ChronosNav& nav, core::AppModel& model);
  void OnIconChunk(const ChronosMessage& message, core::AppModel& model);
  void PublishIcon(core::AppModel& model) const;
  void PublishMedia(core::AppModel& model) const;

  bool connected_ = false;

  bool clock_valid_ = false;
  uint32_t clock_base_s_ = 0;  // seconds since midnight at the last sync
  uint32_t clock_sync_ms_ = 0;

  bool nav_has_icon_ = false;
  uint32_t nav_icon_crc_ = 0;
  uint32_t icon_crc_ = 0;    // of the chunks collected so far
  uint8_t icon_chunks_ = 0;  // bit per chunk received
  std::array<uint8_t, core::kManeuverIconBytes> icon_bits_{};

  bool has_player_ = false;  // Chronos sends an empty package name without a media session
  core::MediaState media_{};
};

}  // namespace sarathi::link
