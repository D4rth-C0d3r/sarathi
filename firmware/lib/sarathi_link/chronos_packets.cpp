#include "chronos_packets.h"

#include <cstring>

// The phone's writes arrive as a pointer and a length.
// NOLINTBEGIN(cppcoreguidelines-pro-bounds-pointer-arithmetic)

namespace sarathi::link {

namespace {

constexpr uint8_t kStart = 0xAB;
constexpr uint8_t kStartOther = 0xEA;  // another packet family; reassembled, never decoded
constexpr uint8_t kFlagFe = 0xFE;
constexpr uint8_t kFlagFf = 0xFF;
constexpr size_t kHeader = 3;  // start byte and the two length bytes
constexpr size_t kMinPacket = 6;

constexpr uint8_t kCmdBattery = 0x91;
constexpr uint8_t kCmdTime = 0x93;
constexpr uint8_t kCmdRinger = 0x72;
constexpr uint8_t kCmdMusic = 0x9D;
constexpr uint8_t kCmdNavIcon = 0xEE;
constexpr uint8_t kCmdNav = 0xEF;

constexpr uint8_t kRingerStart = 0x01;
constexpr uint8_t kRingerStop = 0x02;
constexpr uint8_t kMusicState = 0x80;
constexpr uint8_t kMusicTitle = 0x81;
constexpr uint8_t kMusicArtist = 0x82;
constexpr uint8_t kNavInactive = 0x00;
constexpr uint8_t kNavData = 0x80;
constexpr uint8_t kNavDisabled = 0xFF;  // navigation is off in the Chronos app's settings

bool IsFirstWrite(const uint8_t* data, size_t size) {
  return size >= kMinPacket - 1 && (data[0] == kStart || data[0] == kStartOther) &&
         (data[3] == kFlagFe || data[3] == kFlagFf);
}

uint32_t ReadU32(const uint8_t* p) {
  return (static_cast<uint32_t>(p[0]) << 24U) | (static_cast<uint32_t>(p[1]) << 16U) |
         (static_cast<uint32_t>(p[2]) << 8U) | static_cast<uint32_t>(p[3]);
}

// A text field from `pos` up to a NUL or the end; `pos` moves past the NUL.
std::string_view ReadField(const uint8_t* buf, size_t& pos, size_t end) {
  const size_t start = pos;
  while (pos < end && buf[pos] != 0) {
    ++pos;
  }
  const std::string_view field(reinterpret_cast<const char*>(buf + start), pos - start);
  if (pos < end) {
    ++pos;
  }
  return field;
}

}  // namespace

void ChronosReceiver::Drop(ChronosDrop reason, size_t write_size, uint8_t command,
                           size_t expected) {
  last_drop_.reason = reason;
  last_drop_.command = command;
  last_drop_.write_size = static_cast<uint16_t>(write_size);
  last_drop_.expected = static_cast<uint16_t>(expected);
  last_drop_.received = static_cast<uint16_t>(received_);
  expected_ = 0;
  received_ = 0;
  ++dropped_;
}

bool ChronosReceiver::Feed(const uint8_t* data, size_t size, ChronosMessage& out) {
  if (data == nullptr || size == 0) {
    return false;
  }
  if (IsFirstWrite(data, size)) {
    if (expected_ != 0) {
      Drop(ChronosDrop::kCutShort, size, buf_[4], expected_);  // the previous one never finished
    }
    const size_t total = ((static_cast<size_t>(data[1]) << 8U) | data[2]) + kHeader;
    // A packet longer than its first write must start with exactly kChronosFirstWrite bytes,
    // or the continuation offsets don't line up.
    if (total < kMinPacket || total > kChronosMaxPacket ||
        (size < total && size != kChronosFirstWrite)) {
      Drop(ChronosDrop::kBadLength, size, data[4], total);
      return false;
    }
    const size_t n = size < total ? size : total;  // extra bytes past the length are ignored
    std::memcpy(buf_.data(), data, n);
    expected_ = total;
    received_ = n;
    next_seq_ = 0;
  } else {
    if (expected_ == 0) {
      Drop(ChronosDrop::kStray, size, 0, 0);  // a continuation with no packet in progress
      return false;
    }
    const size_t payload = size - 1;
    if (data[0] != next_seq_ || payload == 0 || payload > kChronosNextWrite ||
        received_ != kChronosFirstWrite + (static_cast<size_t>(next_seq_) * kChronosNextWrite) ||
        received_ + payload > expected_) {
      Drop(ChronosDrop::kOutOfOrder, size, buf_[4], expected_);
      return false;
    }
    std::memcpy(buf_.data() + received_, data + 1, payload);
    received_ += payload;
    ++next_seq_;
  }
  return received_ == expected_ && Complete(out);
}

bool ChronosReceiver::Complete(ChronosMessage& out) {
  length_ = expected_;
  expected_ = 0;
  received_ = 0;
  out = ChronosMessage{};
  return Decode(out);
}

bool ChronosReceiver::Decode(ChronosMessage& out) const {
  const uint8_t* b = buf_.data();
  const size_t len = length_;
  if (b[0] != kStart) {
    return false;
  }
  const bool fe = b[3] == kFlagFe;
  switch (b[4]) {
    case kCmdTime: {
      constexpr size_t kTimeLength = 14;
      if (len < kTimeLength) {
        return false;
      }
      ChronosTime t;
      t.year = static_cast<uint16_t>((b[7] << 8U) | b[8]);
      t.month = b[9];
      t.day = b[10];
      t.hour = b[11];
      t.minute = b[12];
      t.second = b[13];
      constexpr uint8_t kMonths = 12;
      constexpr uint8_t kDays = 31;
      constexpr uint8_t kHours = 24;
      constexpr uint8_t kSixty = 60;
      if (t.month < 1 || t.month > kMonths || t.day < 1 || t.day > kDays || t.hour >= kHours ||
          t.minute >= kSixty || t.second >= kSixty) {
        return false;
      }
      out.kind = ChronosKind::kTime;
      out.time = t;
      return true;
    }
    case kCmdBattery: {
      constexpr size_t kBatteryLength = 8;
      if (!fe || len < kBatteryLength) {
        return false;
      }
      constexpr uint8_t kFull = 100;
      out.kind = ChronosKind::kPhoneBattery;
      out.charging = b[6] == 1;
      out.battery_percent = b[7] > kFull ? kFull : b[7];
      return true;
    }
    case kCmdRinger: {
      constexpr size_t kTextStart = 8;
      if (len < kTextStart) {
        return false;
      }
      if (b[6] == kRingerStart) {
        size_t pos = kTextStart;
        out.kind = ChronosKind::kRinger;
        out.ringing = true;
        out.text = ReadField(b, pos, len);
        return true;
      }
      if (b[6] == kRingerStop) {
        out.kind = ChronosKind::kRinger;
        out.ringing = false;
        return true;
      }
      return false;  // an app notification: not shown while riding
    }
    case kCmdMusic: {
      if (!fe || len < kMinPacket) {
        return false;
      }
      if (b[5] == kMusicState) {
        constexpr size_t kNameStart = 13;
        if (len < kNameStart) {
          return false;
        }
        size_t pos = kNameStart;
        (void)ReadField(b, pos, len);  // the player's display name
        out.kind = ChronosKind::kMusicState;
        out.playing = b[6] == 1;
        out.text = ReadField(b, pos, len);  // package name; empty without a player
        return true;
      }
      if (b[5] == kMusicTitle || b[5] == kMusicArtist) {
        constexpr size_t kTextStart = 7;  // b[6] is the text length; the library ignores it
        if (len < kTextStart) {
          return false;
        }
        size_t pos = kTextStart;
        out.kind = b[5] == kMusicTitle ? ChronosKind::kMusicTitle : ChronosKind::kMusicArtist;
        out.text = ReadField(b, pos, len);
        return true;
      }
      return false;
    }
    case kCmdNav: {
      if (!fe) {
        return false;
      }
      out.kind = ChronosKind::kNav;
      if (b[5] == kNavInactive || b[5] == kNavDisabled) {
        return true;  // out.nav.active stays false
      }
      constexpr size_t kTextStart = 12;
      if (b[5] != kNavData || len < kTextStart) {
        return false;
      }
      ChronosNav& nav = out.nav;
      nav.active = true;
      nav.has_icon = b[6] == 1;
      nav.is_navigation = b[7] == 1;
      nav.icon_crc = ReadU32(b + 8);
      size_t pos = kTextStart;
      nav.title = ReadField(b, pos, len);
      nav.duration = ReadField(b, pos, len);
      nav.distance = ReadField(b, pos, len);
      nav.eta = ReadField(b, pos, len);
      nav.directions = ReadField(b, pos, len);
      return true;  // a speed field may follow (OsmAnd only); unused
    }
    case kCmdNavIcon: {
      constexpr size_t kBitsStart = 11;
      if (!fe || len < kBitsStart + kChronosIconChunkBytes || b[6] >= kChronosIconChunks) {
        return false;
      }
      out.kind = ChronosKind::kNavIcon;
      out.icon_chunk = b[6];
      out.icon_crc = ReadU32(b + 7);
      out.icon_bits = b + kBitsStart;
      return true;
    }
    default:
      return false;
  }
}

}  // namespace sarathi::link

// NOLINTEND(cppcoreguidelines-pro-bounds-pointer-arithmetic)
