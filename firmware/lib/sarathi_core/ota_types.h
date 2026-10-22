// OTA manifest types and error codes (docs/ota/manifest.md, ADR-0004). Pure data.

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "fixed_string.h"

namespace sarathi::core {

// The spec's error codes, in the order its verification steps can produce them.
enum class OtaError : uint8_t {
  kNone,
  kMalformed,
  kUnsupportedAlg,
  kUnsupportedType,
  kUnknownKid,
  kBadSignature,
  kBadSchema,
  kWrongBoard,
  kNotNewer,
  kTooLarge,
  kInsecureUrl,
  kSizeMismatch,
  kHashMismatch,
};

// The spec's spelling ("ok", "malformed", ...), as used in logs and the golden vectors.
[[nodiscard]] const char* OtaErrorCode(OtaError error);

constexpr size_t kMaxManifestBytes = 2048;
// Base64url of at most 2048 characters decodes to at most 1536 bytes.
constexpr size_t kMaxManifestPartBytes = kMaxManifestBytes / 4 * 3;
constexpr size_t kSha256Bytes = 32;
constexpr size_t kEs256SignatureBytes = 64;

using Sha256 = std::array<uint8_t, kSha256Bytes>;

// A verified manifest's payload.
struct Manifest {
  FixedString<32> kid;  // the key that signed it
  FixedString<32> board;
  FixedString<11> version;  // "999.99.99" at most
  uint32_t seq = 0;
  uint64_t size = 0;
  Sha256 sha256{};
  uint32_t min_protocol = 0;
  FixedString<kMaxManifestPartBytes> url;  // anything that fits in the payload
};

}  // namespace sarathi::core
