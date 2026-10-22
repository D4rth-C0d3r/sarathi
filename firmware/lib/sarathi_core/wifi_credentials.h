// Saved Wi-Fi network (ADR-0004 §2): validation and a small versioned blob for NVS. Pure logic.

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

#include "fixed_string.h"

namespace sarathi::core {

constexpr size_t kMaxSsidBytes = 32;      // IEEE 802.11
constexpr size_t kMaxPasswordBytes = 64;  // WPA2: 8-63 characters, or 64 hex digits

struct WifiCredentials {
  FixedString<kMaxSsidBytes> ssid;
  FixedString<kMaxPasswordBytes> password;  // empty for an open network
};

// 1-32 bytes, no NUL.
[[nodiscard]] bool ValidSsid(std::string_view ssid);
// Empty (open network), 8-63 printable ASCII characters, or exactly 64 hex digits.
[[nodiscard]] bool ValidWifiPassword(std::string_view password);

constexpr uint8_t kWifiBlobVersion = 1;
// [0] version, [1] SSID length, [2..33] SSID, [34] password length, [35..98] password.
constexpr size_t kWifiBlobSize = 1 + 1 + kMaxSsidBytes + 1 + kMaxPasswordBytes;
using WifiBlob = std::array<uint8_t, kWifiBlobSize>;

[[nodiscard]] WifiBlob EncodeWifiCredentials(const WifiCredentials& credentials);

// False (and `*out` untouched) for a wrong size or version, or values that aren't valid.
[[nodiscard]] bool DecodeWifiCredentials(const uint8_t* data, size_t size, WifiCredentials* out);

}  // namespace sarathi::core
