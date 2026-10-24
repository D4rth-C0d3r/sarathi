// Chronos app packets (phone -> device), as fbiego/chronos-esp32 1.9.1 receives them
// (docs/phase3-findings.md). Reassembles the phone's BLE writes, with every length, offset and
// index checked, and decodes only the commands sārathi uses. The library's own parser is never
// called: it trusts lengths and indices from the packet.
//
// Pure C++: no Chronos, NimBLE or Arduino headers, so it runs in native tests against recorded
// packets.
//
// Packet: 0xAB, length (2 bytes, big endian, = total - 3), 0xFE or 0xFF, command, ... A packet
// longer than one write arrives as a 20-byte first write, then writes of a sequence number
// (0, 1, ...) and up to 19 bytes.

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace sarathi::link {

constexpr size_t kChronosMaxPacket = 512;  // the library's CS_DATA_SIZE
constexpr size_t kChronosFirstWrite = 20;
constexpr size_t kChronosNextWrite = 19;  // payload after the sequence number
constexpr size_t kChronosIconChunks = 3;
constexpr size_t kChronosIconChunkBytes = 96;

enum class ChronosKind : uint8_t {
  kTime,          // the phone's local time
  kPhoneBattery,  // level and charging
  kRinger,        // incoming call started (with the caller) or stopped ringing
  kMusicState,    // playing or paused, and the player's package name
  kMusicTitle,
  kMusicArtist,
  kNav,      // navigation status and texts
  kNavIcon,  // one third of the 48x48 maneuver icon
};

struct ChronosTime {
  uint16_t year = 0;
  uint8_t month = 0;  // 1-12
  uint8_t day = 0;    // 1-31
  uint8_t hour = 0;
  uint8_t minute = 0;
  uint8_t second = 0;
};

struct ChronosNav {
  bool active = false;         // a navigation notification is present
  bool is_navigation = false;  // turn-by-turn, not general info
  bool has_icon = false;
  uint32_t icon_crc = 0;
  std::string_view title;       // distance to the next maneuver, e.g. "200 m"
  std::string_view duration;    // remaining time
  std::string_view distance;    // remaining distance
  std::string_view eta;         // arrival time
  std::string_view directions;  // instruction or street
};

// Decoded packet. Text views point into the receiver and stay valid until its next Feed().
// Text is as sent: not yet checked for UTF-8 (see chronos_bridge).
struct ChronosMessage {
  ChronosKind kind = ChronosKind::kTime;
  ChronosTime time{};
  uint8_t battery_percent = 0;
  bool charging = false;
  bool ringing = false;
  bool playing = false;
  std::string_view text;  // the caller, track title or artist, or the player's package name
  ChronosNav nav{};
  uint8_t icon_chunk = 0;  // 0-2
  uint32_t icon_crc = 0;
  const uint8_t* icon_bits = nullptr;  // kChronosIconChunkBytes
};

enum class ChronosDrop : uint8_t {
  kNone,
  kBadLength,   // declared length out of range, or a split packet not starting with 20 bytes
  kCutShort,    // a new packet started before the previous one finished
  kStray,       // a continuation with no packet in progress
  kOutOfOrder,  // wrong sequence number, size or offset
};

// What the last dropped packet looked like: header only, never its content.
struct ChronosDropInfo {
  ChronosDrop reason = ChronosDrop::kNone;
  uint8_t command = 0;  // of the packet being assembled, or of the rejected first write
  uint16_t write_size = 0;
  uint16_t expected = 0;  // declared total length
  uint16_t received = 0;
};

class ChronosReceiver {
 public:
  // One BLE write from the phone. Returns true when it completes a packet sārathi uses.
  bool Feed(const uint8_t* data, size_t size, ChronosMessage& out);

  // Packets dropped: malformed, cut short, out of order, or with out-of-range values.
  [[nodiscard]] uint32_t Dropped() const { return dropped_; }
  [[nodiscard]] const ChronosDropInfo& LastDrop() const { return last_drop_; }

 private:
  bool Complete(ChronosMessage& out);
  bool Decode(ChronosMessage& out) const;
  void Drop(ChronosDrop reason, size_t write_size, uint8_t command, size_t expected);

  std::array<uint8_t, kChronosMaxPacket> buf_{};
  size_t expected_ = 0;  // total length of the packet being assembled; 0 when idle
  size_t received_ = 0;
  size_t length_ = 0;  // of the last complete packet
  uint8_t next_seq_ = 0;
  uint32_t dropped_ = 0;
  ChronosDropInfo last_drop_{};
};

}  // namespace sarathi::link
