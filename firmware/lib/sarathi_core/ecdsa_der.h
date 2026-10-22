// JWS carries ES256 signatures as raw r || s (RFC 7518 §3.4); mbedTLS verifies the DER form
// (SEQUENCE of two INTEGERs, RFC 3279). Pure logic.

#pragma once

#include <cstddef>
#include <cstdint>

namespace sarathi::core {

// A P-256 signature in DER is at most 2 + 2 * (2 + 33) bytes.
constexpr size_t kMaxEcdsaP256DerBytes = 72;

// Converts a 64-byte raw signature to minimal DER: leading zero bytes dropped, and a 0x00
// prefix where an INTEGER's high bit is set. Returns false if `capacity` is too small.
[[nodiscard]] bool EcdsaRawToDer(const uint8_t* raw, uint8_t* out, size_t capacity, size_t* length);

}  // namespace sarathi::core
