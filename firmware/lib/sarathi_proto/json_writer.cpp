#include "json_writer.h"

namespace sarathi::proto {

void JsonWriter::Put(char c) {
  if (!ok_ || size_ >= capacity_) {
    ok_ = false;
    return;
  }
  out_[size_++] = c;  // NOLINT(cppcoreguidelines-pro-bounds-pointer-arithmetic)
}

void JsonWriter::Put(std::string_view s) {
  for (const char c : s) {
    Put(c);
  }
}

void JsonWriter::Separator() {
  if (after_key_) {
    after_key_ = false;
    return;
  }
  if (depth_ > 0) {
    if (has_items_.at(depth_ - 1)) {
      Put(',');
    }
    has_items_.at(depth_ - 1) = true;
  }
}

void JsonWriter::Open(char c) {
  Separator();
  Put(c);
  if (depth_ >= kMaxDepth) {
    ok_ = false;
    return;
  }
  has_items_.at(depth_++) = false;
}

void JsonWriter::Close(char c) {
  if (depth_ == 0) {
    ok_ = false;
    return;
  }
  --depth_;
  Put(c);
}

void JsonWriter::Key(std::string_view name) {
  String(name);
  Put(':');
  after_key_ = true;
}

void JsonWriter::Value(std::string_view raw) {
  Separator();
  Put(raw);
}

void JsonWriter::String(std::string_view text) {
  Separator();
  Put('"');
  for (const char c : text) {
    const auto u = static_cast<uint8_t>(c);
    switch (c) {
      case '"':
        Put("\\\"");
        break;
      case '\\':
        Put("\\\\");
        break;
      case '\b':
        Put("\\b");
        break;
      case '\t':
        Put("\\t");
        break;
      case '\n':
        Put("\\n");
        break;
      case '\f':
        Put("\\f");
        break;
      case '\r':
        Put("\\r");
        break;
      default:
        if (u < 0x20) {
          constexpr std::string_view kHex = "0123456789abcdef";
          Put("\\u00");
          Put(kHex[u >> 4U]);
          Put(kHex[u & 0x0FU]);
        } else {
          Put(c);
        }
    }
  }
  Put('"');
}

void JsonWriter::Uint(uint64_t value) {
  std::array<char, 20> digits{};
  size_t n = 0;
  do {
    digits.at(n++) = static_cast<char>('0' + (value % 10));
    value /= 10;
  } while (value > 0);
  Separator();
  while (n > 0) {
    Put(digits.at(--n));
  }
}

void JsonWriter::Int(int64_t value) {
  if (value >= 0) {
    Uint(static_cast<uint64_t>(value));
    return;
  }
  // Writes the sign as part of the same value, then the magnitude without a separator.
  Separator();
  Put('-');
  after_key_ = true;
  Uint(static_cast<uint64_t>(-(value + 1)) + 1U);
}

}  // namespace sarathi::proto
