// Bounded, heap-free string for app state (no std::string, no Arduino String).

#pragma once

#include <array>
#include <cstddef>
#include <cstring>
#include <string_view>

namespace sarathi::core {

// Holds up to N bytes of UTF-8 plus a terminator. Longer input is truncated, never in the
// middle of a multi-byte character.
template <size_t N>
class FixedString {
 public:
  FixedString() = default;
  explicit FixedString(std::string_view text) { Assign(text); }

  void Assign(std::string_view text) {
    size_t n = text.size() < N ? text.size() : N;
    if (n < text.size()) {
      // Back off UTF-8 continuation bytes (10xxxxxx) so a character is never split.
      while (n > 0 && (static_cast<unsigned char>(text[n]) & 0xC0U) == 0x80U) {
        --n;
      }
    }
    std::memcpy(buf_.data(), text.data(), n);
    buf_[n] = '\0';
    size_ = n;
  }

  [[nodiscard]] const char* CStr() const { return buf_.data(); }
  [[nodiscard]] std::string_view View() const { return {buf_.data(), size_}; }
  [[nodiscard]] size_t Size() const { return size_; }
  [[nodiscard]] bool Empty() const { return size_ == 0; }

  bool operator==(const FixedString& other) const { return View() == other.View(); }
  bool operator!=(const FixedString& other) const { return !(*this == other); }

 private:
  std::array<char, N + 1> buf_{};
  size_t size_ = 0;
};

}  // namespace sarathi::core
