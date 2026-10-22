// Firmware versions as OTA sequence numbers (docs/ota/manifest.md): MAJOR.MINOR.PATCH with
// MAJOR 0-999 and MINOR, PATCH 0-99, no leading zeros, and seq = MAJOR*10000 + MINOR*100 +
// PATCH. constexpr, so version.h can check its build flags at compile time.

#pragma once

#include <cstdint>
#include <string_view>

namespace sarathi::core {

constexpr int64_t kInvalidSeq = -1;

namespace detail {

// Parses one number field of at most `max_digits` digits. Returns -1 if invalid.
constexpr int64_t VersionField(std::string_view text, size_t max_digits) {
  if (text.empty() || text.size() > max_digits || (text.size() > 1 && text[0] == '0')) {
    return -1;
  }
  int64_t value = 0;
  for (const char c : text) {
    if (c < '0' || c > '9') {
      return -1;
    }
    value = (value * 10) + (c - '0');
  }
  return value;
}

}  // namespace detail

// The seq for "MAJOR.MINOR.PATCH", or kInvalidSeq.
constexpr int64_t SeqForVersion(std::string_view version) {
  const size_t first = version.find('.');
  if (first == std::string_view::npos) {
    return kInvalidSeq;
  }
  const size_t second = version.find('.', first + 1);
  if (second == std::string_view::npos) {
    return kInvalidSeq;
  }
  const int64_t major = detail::VersionField(version.substr(0, first), 3);
  const int64_t minor = detail::VersionField(version.substr(first + 1, second - first - 1), 2);
  const int64_t patch = detail::VersionField(version.substr(second + 1), 2);
  if (major < 0 || minor < 0 || patch < 0) {
    return kInvalidSeq;
  }
  return (major * 10000) + (minor * 100) + patch;
}

}  // namespace sarathi::core
