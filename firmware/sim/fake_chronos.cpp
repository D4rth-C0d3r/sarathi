#include "fake_chronos.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <string_view>

#include "maneuver_glyph.h"

namespace sarathi::sim {
namespace {

using Bytes = std::vector<uint8_t>;

constexpr uint32_t kNavStepMs = 1500;
constexpr float kStrokeHalfWidth = 6.0F;  // in glyph units (100 across)

struct NavStep {
  core::ManeuverType maneuver;
  const char* distance;
  const char* instruction;
  const char* remaining;
};

constexpr std::array<NavStep, 6> kRoute = {{
    {core::ManeuverType::kLeft, "300 m", "Turn left onto Station Road", "14 min"},
    {core::ManeuverType::kLeft, "200 m", "Turn left onto Station Road", "14 min"},
    {core::ManeuverType::kLeft, "100 m", "Turn left onto Station Road", "13 min"},
    {core::ManeuverType::kSlightRight, "1.2 km", "Keep right towards Mahatma Gandhi Road",
     "13 min"},
    {core::ManeuverType::kSlightRight, "600 m", "Keep right towards Mahatma Gandhi Road", "12 min"},
    {core::ManeuverType::kRoundabout, "400 m", "At the roundabout, take the 2nd exit", "11 min"},
}};

struct Track {
  const char* title;
  const char* artist;
};

constexpr std::array<Track, 2> kTracks = {{
    {"Kesariya", "Arijit Singh"},
    {"Raabta", "Pritam"},
}};

Bytes Packet(uint8_t flag, uint8_t command, const Bytes& body) {
  const size_t length = 2 + body.size();
  Bytes p = {0xAB, static_cast<uint8_t>(length >> 8U), static_cast<uint8_t>(length & 0xFFU), flag,
             command};
  p.insert(p.end(), body.begin(), body.end());
  return p;
}

void AppendText(Bytes& b, std::string_view text) {
  b.insert(b.end(), text.begin(), text.end());
  b.push_back(0);
}

void AppendU32(Bytes& b, uint32_t v) {
  for (int shift = 24; shift >= 0; shift -= 8) {
    b.push_back(static_cast<uint8_t>(v >> static_cast<unsigned>(shift)));
  }
}

float SegmentDistance(float px, float py, core::GlyphPoint a, core::GlyphPoint b) {
  const float ax = a.x;
  const float ay = a.y;
  const float dx = static_cast<float>(b.x) - ax;
  const float dy = static_cast<float>(b.y) - ay;
  const float len2 = (dx * dx) + (dy * dy);
  float t = len2 > 0.0F ? (((px - ax) * dx) + ((py - ay) * dy)) / len2 : 0.0F;
  t = t < 0.0F ? 0.0F : (t > 1.0F ? 1.0F : t);
  return std::hypot(px - (ax + (t * dx)), py - (ay + (t * dy)));
}

// Stands in for the phone's picture: our own glyph, rasterised to 48x48, 1 bit per pixel.
core::ManeuverIcon RasteriseIcon(core::ManeuverType type) {
  const core::ManeuverGlyph glyph = core::GlyphFor(core::Maneuver{type, 2});
  core::ManeuverIcon icon;
  const float scale = static_cast<float>(core::kGlyphSize) / core::kManeuverIconSize;
  for (size_t y = 0; y < core::kManeuverIconSize; ++y) {
    for (size_t x = 0; x < core::kManeuverIconSize; ++x) {
      const float gx = (static_cast<float>(x) + 0.5F) * scale;
      const float gy = (static_cast<float>(y) + 0.5F) * scale;
      bool on = false;
      for (size_t s = 0; s < glyph.stroke_count && !on; ++s) {
        const core::GlyphStroke& stroke = glyph.strokes.at(s);
        for (size_t i = 0; i + 1 < stroke.count && !on; ++i) {
          on = SegmentDistance(gx, gy, stroke.points.at(i), stroke.points.at(i + 1)) <=
               kStrokeHalfWidth;
        }
      }
      if (glyph.has_ring && !on) {
        const float r = std::hypot(gx - glyph.ring_center.x, gy - glyph.ring_center.y);
        on = std::fabs(r - static_cast<float>(glyph.ring_radius)) <= kStrokeHalfWidth;
      }
      if (on) {
        const size_t bit = (y * core::kManeuverIconSize) + x;
        icon.bits.at(bit / 8) |= static_cast<uint8_t>(0x80U >> (bit % 8U));
      }
    }
  }
  return icon;
}

uint32_t IconCrc(core::ManeuverType type) { return 0x51A10000U + static_cast<uint32_t>(type); }

}  // namespace

// Over a small MTU the app splits packets: 20 bytes, then a sequence number and 19 bytes.
void FakeChronos::Write(const Bytes& packet, uint32_t now_ms, core::AppModel& model) {
  std::vector<Bytes> writes;
  if (packet.size() <= link::kChronosFirstWrite) {
    writes.push_back(packet);
  } else {
    writes.emplace_back(packet.begin(), packet.begin() + link::kChronosFirstWrite);
    uint8_t seq = 0;
    for (size_t at = link::kChronosFirstWrite; at < packet.size(); at += link::kChronosNextWrite) {
      Bytes w = {seq++};
      const size_t end = std::min(at + link::kChronosNextWrite, packet.size());
      w.insert(w.end(), packet.begin() + static_cast<std::ptrdiff_t>(at),
               packet.begin() + static_cast<std::ptrdiff_t>(end));
      writes.push_back(w);
    }
  }
  for (const Bytes& w : writes) {
    link::ChronosMessage message;
    if (receiver_.Feed(w.data(), w.size(), message)) {
      bridge_.OnMessage(message, now_ms, model);
    }
  }
}

void FakeChronos::SendNav(uint32_t now_ms, core::AppModel& model) {
  const NavStep& step = kRoute.at(nav_step_);
  const uint32_t crc = IconCrc(step.maneuver);
  Bytes nav = {0x80, 1, 1};
  AppendU32(nav, crc);
  AppendText(nav, step.distance);
  AppendText(nav, step.remaining);
  AppendText(nav, "4.5 km");
  AppendText(nav, "19:10");
  AppendText(nav, step.instruction);
  AppendText(nav, "");
  Write(Packet(0xFE, 0xEF, nav), now_ms, model);

  const core::ManeuverIcon icon = RasteriseIcon(step.maneuver);
  for (uint8_t chunk = 0; chunk < link::kChronosIconChunks; ++chunk) {
    Bytes body = {0x80, chunk};
    AppendU32(body, crc);
    const auto* bits = icon.bits.data() + (chunk * link::kChronosIconChunkBytes);
    body.insert(body.end(), bits, bits + link::kChronosIconChunkBytes);
    Write(Packet(0xFE, 0xEE, body), now_ms, model);
  }
}

void FakeChronos::SendTrack(uint32_t now_ms, core::AppModel& model) {
  Bytes state = {0x80, static_cast<uint8_t>(playing_ ? 1 : 0), 0, 0, 0, 0xFF, 0xFF, 0xFF};
  AppendText(state, "Player");
  AppendText(state, "com.example.player");
  Write(Packet(0xFE, 0x9D, state), now_ms, model);
  const Track& track = kTracks.at(track_);
  Bytes title = {0x81, 0};
  AppendText(title, track.title);
  Write(Packet(0xFE, 0x9D, title), now_ms, model);
  Bytes artist = {0x82, 0};
  AppendText(artist, track.artist);
  Write(Packet(0xFE, 0x9D, artist), now_ms, model);
}

void FakeChronos::SetConnected(bool connected, uint32_t now_ms, core::AppModel& model) {
  connected_ = connected;
  bridge_.OnConnection(connected, model);
  if (connected) {
    Write(Packet(0xFF, 0x93, {0x80, 0x00, 0x07, 0xEA, 10, 24, 18, 55, 0}), now_ms, model);
    Write(Packet(0xFE, 0x91, {0x80, 0x00, 64}), now_ms, model);
    SendTrack(now_ms, model);
    SendNav(now_ms, model);
    next_nav_ms_ = now_ms + kNavStepMs;
  }
}

void FakeChronos::RequestPairing(uint32_t code, uint32_t now_ms, core::AppModel& model) {
  bridge_.OnPairingRequest(code, now_ms, model);
}

void FakeChronos::SetNavigating(bool navigating, uint32_t now_ms, core::AppModel& model) {
  navigating_ = navigating;
  if (navigating) {
    SendNav(now_ms, model);
  } else {
    Write(Packet(0xFE, 0xEF, {0x00}), now_ms, model);
  }
}

void FakeChronos::Poll(uint32_t now_ms, core::AppModel& model) {
  if (!started_) {
    started_ = true;
    bridge_.Begin(model);
    bridge_.OnPairedPhones(paired_, model);
    SetConnected(true, now_ms, model);
  }
  if (connected_ && navigating_ && now_ms >= next_nav_ms_) {
    nav_step_ = (nav_step_ + 1) % kRoute.size();
    SendNav(now_ms, model);
    next_nav_ms_ = now_ms + kNavStepMs;
  }
  bridge_.Tick(now_ms, model);
}

void FakeChronos::OnCommand(const core::Command& command, uint32_t now_ms, core::AppModel& model) {
  switch (bridge_.OnCommand(command, now_ms, model)) {
    case link::ChronosAction::kPairAccept:
      paired_ = static_cast<uint8_t>(paired_ + 1);
      bridge_.OnPairedPhones(paired_, model);
      return;
    case link::ChronosAction::kPairReject:
      return;
    case link::ChronosAction::kForgetPhones:
      paired_ = 0;
      bridge_.OnPairedPhones(paired_, model);
      return;
    case link::ChronosAction::kMusicToggle:
      playing_ = !playing_;
      break;
    case link::ChronosAction::kMusicNext:
    case link::ChronosAction::kMusicPrevious:
      track_ = (track_ + 1) % kTracks.size();
      break;
    case link::ChronosAction::kNone:
      return;
  }
  SendTrack(now_ms, model);  // the phone reports the result
}

}  // namespace sarathi::sim
