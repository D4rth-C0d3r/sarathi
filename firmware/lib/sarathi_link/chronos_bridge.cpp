#include "chronos_bridge.h"

#include <cstddef>
#include <cstring>
#include <string_view>

#include "fixed_string.h"

namespace sarathi::link {

namespace {

constexpr uint32_t kSecondsPerMinute = 60;
constexpr uint32_t kSecondsPerDay = 24U * 60U * 60U;
constexpr uint32_t kMsPerSecond = 1000;
constexpr uint8_t kAllIconChunks = (1U << kChronosIconChunks) - 1U;

// Length of the UTF-8 sequence starting with `lead`, or 0 if it can't start one.
size_t Utf8Length(unsigned char lead) {
  if (lead < 0x80U) {
    return 1;
  }
  if ((lead & 0xE0U) == 0xC0U && lead >= 0xC2U) {
    return 2;
  }
  if ((lead & 0xF0U) == 0xE0U) {
    return 3;
  }
  if ((lead & 0xF8U) == 0xF0U && lead <= 0xF4U) {
    return 4;
  }
  return 0;
}

// Phone text is untrusted: copies whole, valid UTF-8 characters; a broken sequence becomes '?'
// and a control character a space. Stops at the destination's size.
template <size_t N>
void AssignText(core::FixedString<N>& out, std::string_view in) {
  std::array<char, N> buf{};
  size_t n = 0;
  size_t i = 0;
  while (i < in.size() && n < N) {
    const auto lead = static_cast<unsigned char>(in[i]);
    size_t len = Utf8Length(lead);
    for (size_t k = 1; k < len; ++k) {
      if (i + k >= in.size() || (static_cast<unsigned char>(in[i + k]) & 0xC0U) != 0x80U) {
        len = 0;
        break;
      }
    }
    if (len == 0) {
      buf.at(n++) = '?';
      ++i;
      continue;
    }
    if (n + len > N) {
      break;  // never split a character
    }
    constexpr unsigned char kFirstPrintable = 0x20;
    constexpr unsigned char kDelete = 0x7F;
    if (len == 1 && (lead < kFirstPrintable || lead == kDelete)) {
      buf.at(n++) = ' ';
    } else {
      std::memcpy(&buf.at(n), &in[i], len);
      n += len;
    }
    i += len;
  }
  out.Assign(std::string_view(buf.data(), n));
}

}  // namespace

void ChronosBridge::Begin(core::AppModel& model) {
  model.SetCapabilities(kChronosCapabilities);
  OnConnection(false, model);
}

void ChronosBridge::OnConnection(bool connected, core::AppModel& model) {
  connected_ = connected;
  model.SetPhoneConnected(connected);
  if (!connected) {
    model.EndNav();
    model.SetCall(core::CallPhase::kIdle, {}, 0);
    model.EndPairingRequest();
    nav_has_icon_ = false;
    icon_chunks_ = 0;
    has_player_ = false;
  }
  PublishMedia(model);
}

void ChronosBridge::OnMessage(const ChronosMessage& message, uint32_t now_ms,
                              core::AppModel& model) {
  switch (message.kind) {
    case ChronosKind::kTime:
      clock_valid_ = true;
      clock_base_s_ = (static_cast<uint32_t>(message.time.hour) * 3600U) +
                      (static_cast<uint32_t>(message.time.minute) * kSecondsPerMinute) +
                      message.time.second;
      clock_sync_ms_ = now_ms;
      Tick(now_ms, model);
      break;
    case ChronosKind::kPhoneBattery:
      model.SetPhoneBattery(message.battery_percent, message.charging);
      break;
    case ChronosKind::kRinger: {
      if (message.ringing) {
        decltype(core::CallState::caller) caller;
        AssignText(caller, message.text);
        model.SetCall(core::CallPhase::kRinging, caller.View(), now_ms);
      } else {
        // Answered, rejected or missed: Chronos can't tell which, so there's no active phase.
        model.SetCall(core::CallPhase::kIdle, {}, now_ms);
      }
      break;
    }
    case ChronosKind::kMusicState:
      has_player_ = !message.text.empty();
      media_.playing = message.playing;
      PublishMedia(model);
      break;
    case ChronosKind::kMusicTitle:
      AssignText(media_.title, message.text);
      PublishMedia(model);
      break;
    case ChronosKind::kMusicArtist:
      AssignText(media_.artist, message.text);
      PublishMedia(model);
      break;
    case ChronosKind::kNav:
      OnNav(message.nav, model);
      break;
    case ChronosKind::kNavIcon:
      OnIconChunk(message, model);
      break;
  }
}

void ChronosBridge::OnNav(const ChronosNav& nav, core::AppModel& model) {
  if (!nav.active || !nav.is_navigation) {
    if (model.State().nav.active) {
      model.EndNav();
    }
    nav_has_icon_ = false;
    return;
  }
  decltype(core::NavState::distance_text) distance;
  decltype(core::NavState::street) instruction;
  decltype(core::NavState::remaining_text) remaining;
  AssignText(distance, nav.title);
  AssignText(instruction, nav.directions);
  AssignText(remaining, nav.duration);
  model.SetPhoneGuidance(distance.View(), instruction.View(), remaining.View());
  nav_has_icon_ = nav.has_icon;
  nav_icon_crc_ = nav.icon_crc;
  PublishIcon(model);
}

void ChronosBridge::OnIconChunk(const ChronosMessage& message, core::AppModel& model) {
  if (message.icon_crc != icon_crc_) {
    icon_crc_ = message.icon_crc;
    icon_chunks_ = 0;
  }
  std::memcpy(&icon_bits_.at(static_cast<size_t>(message.icon_chunk) * kChronosIconChunkBytes),
              message.icon_bits, kChronosIconChunkBytes);
  icon_chunks_ |= static_cast<uint8_t>(1U << message.icon_chunk);
  PublishIcon(model);
}

// The icon shows once all three chunks of the picture the nav status names have arrived.
void ChronosBridge::PublishIcon(core::AppModel& model) const {
  const core::NavState& nav = model.State().nav;
  const bool complete =
      nav_has_icon_ && icon_chunks_ == kAllIconChunks && icon_crc_ == nav_icon_crc_;
  if (!nav.active) {
    return;
  }
  if (!complete) {
    if (nav.icon.valid) {
      model.SetManeuverIcon(core::ManeuverIcon{});
    }
    return;
  }
  if (nav.icon.valid && nav.icon.id == icon_crc_) {
    return;
  }
  core::ManeuverIcon icon;
  icon.valid = true;
  icon.id = icon_crc_;
  icon.bits = icon_bits_;
  model.SetManeuverIcon(icon);
}

void ChronosBridge::PublishMedia(core::AppModel& model) const {
  core::MediaState media = media_;
  media.available = connected_ && has_player_;
  model.SetMedia(media);
}

void ChronosBridge::OnPairingRequest(uint32_t code, uint32_t now_ms, core::AppModel& model) {
  model.SetPairingRequest(code, now_ms);
}

void ChronosBridge::OnPairingEnded(core::AppModel& model) { model.EndPairingRequest(); }

void ChronosBridge::OnPairedPhones(uint8_t count, core::AppModel& model) {
  model.SetPairedPhones(count);
}

void ChronosBridge::Tick(uint32_t now_ms, core::AppModel& model) const {
  const core::PairingState& pairing = model.State().pairing;
  if (pairing.requested && now_ms - pairing.since_ms >= kPairingTimeoutMs) {
    model.EndPairingRequest();
  }
  if (!clock_valid_) {
    return;
  }
  const uint32_t seconds =
      (clock_base_s_ + ((now_ms - clock_sync_ms_) / kMsPerSecond)) % kSecondsPerDay;
  const auto minutes = static_cast<uint16_t>(seconds / kSecondsPerMinute);
  if (!model.State().clock.valid || model.State().clock.minutes_of_day != minutes) {
    model.SetClock(minutes);
  }
}

ChronosAction ChronosBridge::OnCommand(const core::Command& command, uint32_t now_ms,
                                       core::AppModel& model) {
  switch (command.type) {
    case core::CommandType::kCallDismiss:
      model.SetCall(core::CallPhase::kIdle, {}, now_ms);
      return ChronosAction::kNone;
    case core::CommandType::kMediaPlayPause:
      return ChronosAction::kMusicToggle;
    case core::CommandType::kMediaNext:
      return ChronosAction::kMusicNext;
    case core::CommandType::kMediaPrev:
      return ChronosAction::kMusicPrevious;
    case core::CommandType::kPairAccept:
      return ChronosAction::kPairAccept;
    case core::CommandType::kPairReject:
      return ChronosAction::kPairReject;
    case core::CommandType::kForgetPhones:
      return ChronosAction::kForgetPhones;
    case core::CommandType::kCallAccept:
    case core::CommandType::kCallReject:
      break;  // not offered: Chronos has no call control (docs/phase3-findings.md)
  }
  return ChronosAction::kNone;
}

}  // namespace sarathi::link
