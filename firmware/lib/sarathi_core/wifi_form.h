// Helpers for the Wi-Fi setup web page (ADR-0004 §2). Pure logic: parsing the submitted form,
// escaping strings for its JSON endpoints, and making the setup access point's password.

#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

#include "wifi_credentials.h"
#include "wifi_setup.h"

namespace sarathi::core {

enum class WifiFormResult : uint8_t { kOk, kMalformed, kBadSsid, kBadPassword };

// Decodes application/x-www-form-urlencoded text ('+' is a space, %XX a byte). False on a bad
// escape or if the result doesn't fit `capacity` (which includes a terminating NUL).
[[nodiscard]] bool UrlDecode(std::string_view text, char* out, size_t capacity, size_t* length);

// Reads "ssid" and "password" from a form body; other fields are ignored. A missing or
// repeated field is malformed; values are checked with ValidSsid / ValidWifiPassword.
[[nodiscard]] WifiFormResult ParseWifiForm(std::string_view body, WifiCredentials* out);

// Writes `text` as the inside of a JSON string ("..." not included): escapes '"', '\' and
// control characters; other bytes (UTF-8) pass through. False if it doesn't fit (`capacity`
// includes a terminating NUL).
[[nodiscard]] bool JsonEscape(std::string_view text, char* out, size_t capacity, size_t* length);

// Letters and digits that can't be mistaken for each other on the dash (no 0/O, 1/l/I).
constexpr std::string_view kSetupPasswordAlphabet = "abcdefghjkmnpqrstuvwxyz23456789";

// Fills `out` (kSetupPasswordLength + 1 bytes) with a password from `random`, which must be a
// cryptographic source on the device (esp_random).
void MakeSetupPassword(uint32_t (*random)(), char* out);

}  // namespace sarathi::core
