#include "wifi_credentials.h"

#include <cstring>

namespace sarathi::core {
namespace {

constexpr size_t kSsidLength = 1;
constexpr size_t kSsid = 2;
constexpr size_t kPasswordLength = kSsid + kMaxSsidBytes;
constexpr size_t kPassword = kPasswordLength + 1;

bool IsHex(char c) {
  return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

}  // namespace

bool ValidSsid(std::string_view ssid) {
  return !ssid.empty() && ssid.size() <= kMaxSsidBytes && ssid.find('\0') == std::string_view::npos;
}

bool ValidWifiPassword(std::string_view password) {
  if (password.empty()) {
    return true;
  }
  if (password.size() == kMaxPasswordBytes) {
    for (const char c : password) {
      if (!IsHex(c)) {
        return false;
      }
    }
    return true;
  }
  if (password.size() < 8 || password.size() > kMaxPasswordBytes - 1) {
    return false;
  }
  for (const char c : password) {
    if (c < 0x20 || c > 0x7E) {
      return false;
    }
  }
  return true;
}

WifiBlob EncodeWifiCredentials(const WifiCredentials& credentials) {
  WifiBlob blob{};
  blob[0] = kWifiBlobVersion;
  blob[kSsidLength] = static_cast<uint8_t>(credentials.ssid.Size());
  std::memcpy(&blob[kSsid], credentials.ssid.CStr(), credentials.ssid.Size());
  blob[kPasswordLength] = static_cast<uint8_t>(credentials.password.Size());
  std::memcpy(&blob[kPassword], credentials.password.CStr(), credentials.password.Size());
  return blob;
}

bool DecodeWifiCredentials(const uint8_t* data, size_t size, WifiCredentials* out) {
  if (data == nullptr || size != kWifiBlobSize || data[0] != kWifiBlobVersion) {
    return false;
  }
  const size_t ssid_size = data[kSsidLength];          // NOLINT(*-pointer-arithmetic)
  const size_t password_size = data[kPasswordLength];  // NOLINT(*-pointer-arithmetic)
  if (ssid_size > kMaxSsidBytes || password_size > kMaxPasswordBytes) {
    return false;
  }
  // NOLINTNEXTLINE(*-reinterpret-cast,*-pointer-arithmetic)
  const std::string_view ssid(reinterpret_cast<const char*>(data + kSsid), ssid_size);
  // NOLINTNEXTLINE(*-reinterpret-cast,*-pointer-arithmetic)
  const std::string_view password(reinterpret_cast<const char*>(data + kPassword), password_size);
  if (!ValidSsid(ssid) || !ValidWifiPassword(password)) {
    return false;
  }
  out->ssid.Assign(ssid);
  out->password.Assign(password);
  return true;
}

}  // namespace sarathi::core
