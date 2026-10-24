#include "wifi_form.h"

#include <array>
#include <cstdio>

namespace sarathi::core {
namespace {

int HexDigit(char c) {
  if (c >= '0' && c <= '9') {
    return c - '0';
  }
  if (c >= 'a' && c <= 'f') {
    return c - 'a' + 10;
  }
  if (c >= 'A' && c <= 'F') {
    return c - 'A' + 10;
  }
  return -1;
}

}  // namespace

bool UrlDecode(std::string_view text, char* out, size_t capacity, size_t* length) {
  size_t n = 0;
  for (size_t i = 0; i < text.size(); ++i) {
    char c = text[i];
    if (c == '+') {
      c = ' ';
    } else if (c == '%') {
      if (i + 2 >= text.size()) {
        return false;  // "%" needs two hex digits after it
      }
      const int hi = HexDigit(text[i + 1]);
      const int lo = HexDigit(text[i + 2]);
      if (hi < 0 || lo < 0) {
        return false;
      }
      c = static_cast<char>((hi << 4) | lo);
      i += 2;
    }
    if (n + 1 >= capacity) {
      return false;
    }
    out[n++] = c;  // NOLINT(cppcoreguidelines-pro-bounds-pointer-arithmetic)
  }
  out[n] = '\0';  // NOLINT(cppcoreguidelines-pro-bounds-pointer-arithmetic)
  *length = n;
  return true;
}

WifiFormResult ParseWifiForm(std::string_view body, WifiCredentials* out) {
  std::array<char, kMaxSsidBytes + 1> ssid{};
  std::array<char, kMaxPasswordBytes + 1> password{};
  size_t ssid_size = 0;
  size_t password_size = 0;
  bool have_ssid = false;
  bool have_password = false;
  while (!body.empty()) {
    const size_t amp = body.find('&');
    const std::string_view field = body.substr(0, amp);
    body = amp == std::string_view::npos ? std::string_view{} : body.substr(amp + 1);
    const size_t eq = field.find('=');
    if (eq == std::string_view::npos) {
      continue;
    }
    const std::string_view name = field.substr(0, eq);
    const std::string_view value = field.substr(eq + 1);
    if (name == "ssid") {
      if (have_ssid) {
        return WifiFormResult::kMalformed;
      }
      have_ssid = true;
      if (!UrlDecode(value, ssid.data(), ssid.size(), &ssid_size)) {
        return WifiFormResult::kBadSsid;  // too long or a bad escape
      }
    } else if (name == "password") {
      if (have_password) {
        return WifiFormResult::kMalformed;
      }
      have_password = true;
      if (!UrlDecode(value, password.data(), password.size(), &password_size)) {
        return WifiFormResult::kBadPassword;
      }
    }
  }
  if (!have_ssid || !have_password) {
    return WifiFormResult::kMalformed;
  }
  const std::string_view s(ssid.data(), ssid_size);
  const std::string_view p(password.data(), password_size);
  if (!ValidSsid(s)) {
    return WifiFormResult::kBadSsid;
  }
  if (!ValidWifiPassword(p)) {
    return WifiFormResult::kBadPassword;
  }
  out->ssid.Assign(s);
  out->password.Assign(p);
  return WifiFormResult::kOk;
}

bool JsonEscape(std::string_view text, char* out, size_t capacity, size_t* length) {
  size_t n = 0;
  for (const char c : text) {
    std::array<char, 8> piece{};
    size_t size = 0;
    if (c == '"' || c == '\\') {
      piece = {'\\', c};
      size = 2;
    } else if (static_cast<uint8_t>(c) < 0x20) {
      size = static_cast<size_t>(
          std::snprintf(piece.data(), piece.size(), "\\u%04x", static_cast<unsigned>(c)));
    } else {
      piece[0] = c;
      size = 1;
    }
    if (n + size + 1 > capacity) {
      return false;
    }
    for (size_t k = 0; k < size; ++k) {
      out[n++] = piece.at(k);  // NOLINT(cppcoreguidelines-pro-bounds-pointer-arithmetic)
    }
  }
  out[n] = '\0';  // NOLINT(cppcoreguidelines-pro-bounds-pointer-arithmetic)
  *length = n;
  return true;
}

void MakeSetupPassword(uint32_t (*random)(), char* out) {
  const auto size = static_cast<uint32_t>(kSetupPasswordAlphabet.size());
  // Rejection sampling, so every character is equally likely.
  const uint32_t limit = UINT32_MAX - (UINT32_MAX % size);
  for (size_t i = 0; i < kSetupPasswordLength; ++i) {
    uint32_t r = random();
    while (r >= limit) {
      r = random();
    }
    out[i] = kSetupPasswordAlphabet[r % size];  // NOLINT(*-pointer-arithmetic)
  }
  out[kSetupPasswordLength] = '\0';  // NOLINT(*-pointer-arithmetic)
}

}  // namespace sarathi::core
