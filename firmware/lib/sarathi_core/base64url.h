// Strict base64url decoding (RFC 4648 §5, as JWS uses it): URL-safe alphabet, no padding.

#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace sarathi::core {

// Decodes `text` into `out`. Returns false on a character outside the alphabet, padding, an
// impossible length (4n + 1), or output that doesn't fit; `*length` is the decoded size.
// Unused low bits in the last character are ignored, as most decoders do.
[[nodiscard]] bool Base64UrlDecode(std::string_view text, uint8_t* out, size_t capacity,
                                   size_t* length);

}  // namespace sarathi::core
