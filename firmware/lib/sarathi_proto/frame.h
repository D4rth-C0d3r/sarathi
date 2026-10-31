// Framing for the phone ↔ device protocol (docs/protocol/protocol.md section 1, ADR-0007):
// CRC-16, splitting a message into frames for an ATT MTU, and reassembling frames with every
// length, index and identity checked. Pure C++, no allocation.

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace sarathi::proto {

constexpr size_t kMaxMessage = 15360;
constexpr size_t kMaxFragments = 64;
constexpr size_t kFirstHeader = 7;  // type, msg_id, frag, total_len (2), crc (2)
constexpr size_t kNextHeader = 3;   // type, msg_id, frag
constexpr size_t kAttHeader = 3;    // an ATT write or notification carries MTU - 3 bytes
constexpr size_t kMinMtu = 247;
constexpr uint8_t kFirst = 0x80;
constexpr uint8_t kLast = 0x40;
constexpr uint8_t kIndexMask = 0x3F;

// CRC-16/CCITT-FALSE: polynomial 0x1021, initial 0xFFFF, no reflection, no final XOR.
[[nodiscard]] uint16_t Crc16(const uint8_t* data, size_t size);

// Produces the frames of one message, in order, without copying the payload first.
class Fragmenter {
 public:
  // False if the message breaks the limits at this MTU (too long, too many fragments, or an
  // MTU too small for a header and one byte).
  bool Begin(uint8_t type, uint8_t msg_id, const uint8_t* payload, size_t size, size_t mtu);

  // Writes the next frame into `out`. Returns its size, or 0 when the message is done or
  // `capacity` is less than MTU - 3.
  size_t Next(uint8_t* out, size_t capacity);

  [[nodiscard]] size_t FrameCount() const { return frames_; }

 private:
  uint8_t type_ = 0;
  uint8_t msg_id_ = 0;
  const uint8_t* payload_ = nullptr;
  size_t size_ = 0;
  size_t mtu_ = 0;
  size_t sent_bytes_ = 0;
  size_t index_ = 0;
  size_t frames_ = 0;
};

// Why a frame or message was dropped (protocol.md section 1).
enum class Drop : uint8_t {
  kShortFrame,
  kOutOfOrder,
  kTooLong,
  kCutShort,
  kStray,
  kMismatch,
  kLength,
  kCrc,
};

[[nodiscard]] const char* DropName(Drop drop);

struct Message {
  uint8_t type = 0;
  uint8_t msg_id = 0;
  const uint8_t* payload = nullptr;  // see Reassembler::Feed for how long it stays valid
  size_t size = 0;
};

// What one frame led to: at most two drops (an unfinished message abandoned, then this one)
// and at most one complete message, reported after the drops.
struct FeedResult {
  std::array<Drop, 2> drops{};
  size_t drop_count = 0;
  bool has_message = false;
  Message message{};
};

// One multi-frame message at a time; single-frame messages may arrive between its fragments
// and don't disturb it. `storage` holds multi-frame payloads (kMaxMessage bytes; PSRAM on the
// device) and must outlive the reassembler.
class Reassembler {
 public:
  Reassembler(uint8_t* storage, size_t capacity) : storage_(storage), capacity_(capacity) {}

  // A single-frame message's payload points into `frame`; a multi-frame one into `storage`.
  // Either is valid until the next call.
  FeedResult Feed(const uint8_t* frame, size_t size);

  [[nodiscard]] uint32_t Dropped() const { return dropped_; }

 private:
  void Add(FeedResult& result, Drop drop);
  void Complete(FeedResult& result, uint8_t type, uint8_t msg_id, uint16_t total, uint16_t crc,
                const uint8_t* payload, size_t size);

  uint8_t* storage_;
  size_t capacity_;
  bool active_ = false;
  uint8_t type_ = 0;
  uint8_t msg_id_ = 0;
  uint8_t next_index_ = 0;
  uint16_t total_ = 0;
  uint16_t crc_ = 0;
  size_t received_ = 0;
  uint32_t dropped_ = 0;
};

}  // namespace sarathi::proto
