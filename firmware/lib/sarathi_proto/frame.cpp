#include "frame.h"

#include <cstring>

// Frames arrive as a pointer and a length.
// NOLINTBEGIN(cppcoreguidelines-pro-bounds-pointer-arithmetic)

namespace sarathi::proto {
namespace {

uint16_t ReadU16(const uint8_t* p) {
  return static_cast<uint16_t>(p[0] | (static_cast<uint16_t>(p[1]) << 8U));
}

void WriteU16(uint8_t* p, uint16_t v) {
  p[0] = static_cast<uint8_t>(v & 0xFFU);
  p[1] = static_cast<uint8_t>(v >> 8U);
}

}  // namespace

uint16_t Crc16(const uint8_t* data, size_t size) {
  uint16_t crc = 0xFFFF;
  for (size_t i = 0; i < size; ++i) {
    crc ^= static_cast<uint16_t>(data[i] << 8U);
    for (int bit = 0; bit < 8; ++bit) {
      crc = (crc & 0x8000U) != 0 ? static_cast<uint16_t>((crc << 1U) ^ 0x1021U)
                                 : static_cast<uint16_t>(crc << 1U);
    }
  }
  return crc;
}

bool Fragmenter::Begin(uint8_t type, uint8_t msg_id, const uint8_t* payload, size_t size,
                       size_t mtu) {
  frames_ = 0;
  if (size > kMaxMessage || mtu < kAttHeader + kFirstHeader + 1 ||
      (size > 0 && payload == nullptr)) {
    return false;
  }
  const size_t first_cap = mtu - kAttHeader - kFirstHeader;
  const size_t next_cap = mtu - kAttHeader - kNextHeader;
  size_t frames = 1;
  if (size > first_cap) {
    frames += (size - first_cap + next_cap - 1) / next_cap;
  }
  if (frames > kMaxFragments) {
    return false;
  }
  type_ = type;
  msg_id_ = msg_id;
  payload_ = payload;
  size_ = size;
  mtu_ = mtu;
  sent_bytes_ = 0;
  index_ = 0;
  frames_ = frames;
  return true;
}

size_t Fragmenter::Next(uint8_t* out, size_t capacity) {
  if (index_ >= frames_ || capacity < mtu_ - kAttHeader) {
    return 0;
  }
  const bool first = index_ == 0;
  const bool last = index_ + 1 == frames_;
  const size_t header = first ? kFirstHeader : kNextHeader;
  const size_t cap = mtu_ - kAttHeader - header;
  const size_t take = (size_ - sent_bytes_) < cap ? (size_ - sent_bytes_) : cap;
  out[0] = type_;
  out[1] = msg_id_;
  out[2] = static_cast<uint8_t>(index_ | (first ? kFirst : 0U) | (last ? kLast : 0U));
  if (first) {
    WriteU16(out + 3, static_cast<uint16_t>(size_));
    WriteU16(out + 5, Crc16(payload_, size_));
  }
  if (take > 0) {
    std::memcpy(out + header, payload_ + sent_bytes_, take);
  }
  sent_bytes_ += take;
  ++index_;
  return header + take;
}

const char* DropName(Drop drop) {
  switch (drop) {
    case Drop::kShortFrame:
      return "short_frame";
    case Drop::kOutOfOrder:
      return "out_of_order";
    case Drop::kTooLong:
      return "too_long";
    case Drop::kCutShort:
      return "cut_short";
    case Drop::kStray:
      return "stray";
    case Drop::kMismatch:
      return "mismatch";
    case Drop::kLength:
      return "length";
    case Drop::kCrc:
      return "crc";
  }
  return "?";
}

void Reassembler::Add(FeedResult& result, Drop drop) {
  if (result.drop_count < result.drops.size()) {
    result.drops.at(result.drop_count++) = drop;
  }
  ++dropped_;
}

void Reassembler::Complete(FeedResult& result, uint8_t type, uint8_t msg_id, uint16_t total,
                           uint16_t crc, const uint8_t* payload, size_t size) {
  if (size != total) {
    Add(result, Drop::kLength);
    return;
  }
  if (Crc16(payload, size) != crc) {
    Add(result, Drop::kCrc);
    return;
  }
  result.has_message = true;
  result.message = Message{type, msg_id, payload, size};
}

FeedResult Reassembler::Feed(const uint8_t* frame, size_t size) {
  FeedResult result;
  if (frame == nullptr || size < kNextHeader) {
    Add(result, Drop::kShortFrame);
    return result;
  }
  const uint8_t type = frame[0];
  const uint8_t msg_id = frame[1];
  const uint8_t flags = frame[2];
  const uint8_t index = flags & kIndexMask;
  const bool first = (flags & kFirst) != 0;
  const bool last = (flags & kLast) != 0;

  if (first) {
    if (size < kFirstHeader) {
      Add(result, Drop::kShortFrame);
      return result;
    }
    const uint16_t total = ReadU16(frame + 3);
    const uint16_t crc = ReadU16(frame + 5);
    if (index != 0) {
      Add(result, Drop::kOutOfOrder);
      return result;
    }
    if (total > kMaxMessage) {
      Add(result, Drop::kTooLong);
      return result;
    }
    const uint8_t* body = frame + kFirstHeader;
    const size_t body_size = size - kFirstHeader;
    if (last) {  // a single-frame message never disturbs one in progress
      Complete(result, type, msg_id, total, crc, body, body_size);
      return result;
    }
    if (active_) {
      Add(result, Drop::kCutShort);
      active_ = false;
    }
    if (body_size > total || total > capacity_) {
      Add(result, Drop::kTooLong);
      return result;
    }
    std::memcpy(storage_, body, body_size);
    active_ = true;
    type_ = type;
    msg_id_ = msg_id;
    next_index_ = 1;
    total_ = total;
    crc_ = crc;
    received_ = body_size;
    return result;
  }

  if (!active_) {
    Add(result, Drop::kStray);
    return result;
  }
  if (type != type_ || msg_id != msg_id_) {
    active_ = false;
    Add(result, Drop::kMismatch);
    return result;
  }
  if (index != next_index_) {
    active_ = false;
    Add(result, Drop::kOutOfOrder);
    return result;
  }
  const size_t body_size = size - kNextHeader;
  if (received_ + body_size > total_) {
    active_ = false;
    Add(result, Drop::kTooLong);
    return result;
  }
  std::memcpy(storage_ + received_, frame + kNextHeader, body_size);
  received_ += body_size;
  ++next_index_;
  if (!last) {
    return result;
  }
  active_ = false;
  Complete(result, type_, msg_id_, total_, crc_, storage_, received_);
  return result;
}

}  // namespace sarathi::proto

// NOLINTEND(cppcoreguidelines-pro-bounds-pointer-arithmetic)
