#include "base64url.h"

namespace sarathi::core {
namespace {

int SextetOf(char c) {
  if (c >= 'A' && c <= 'Z') {
    return c - 'A';
  }
  if (c >= 'a' && c <= 'z') {
    return c - 'a' + 26;
  }
  if (c >= '0' && c <= '9') {
    return c - '0' + 52;
  }
  if (c == '-') {
    return 62;
  }
  if (c == '_') {
    return 63;
  }
  return -1;
}

}  // namespace

bool Base64UrlDecode(std::string_view text, uint8_t* out, size_t capacity, size_t* length) {
  *length = 0;
  if (text.size() % 4 == 1) {
    return false;
  }
  const size_t needed = (text.size() / 4 * 3) + (text.size() % 4 == 0 ? 0 : text.size() % 4 - 1);
  if (needed > capacity) {
    return false;
  }
  uint32_t bits = 0;
  int bit_count = 0;
  size_t n = 0;
  for (const char c : text) {
    const int sextet = SextetOf(c);
    if (sextet < 0) {
      return false;
    }
    bits = (bits << 6U) | static_cast<uint32_t>(sextet);
    bit_count += 6;
    if (bit_count >= 8) {
      bit_count -= 8;
      out[n++] = static_cast<uint8_t>(bits >> static_cast<uint32_t>(bit_count));  // NOLINT
    }
  }
  *length = n;
  return true;
}

}  // namespace sarathi::core
