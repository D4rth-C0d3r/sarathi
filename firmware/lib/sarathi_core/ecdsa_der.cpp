#include "ecdsa_der.h"

namespace sarathi::core {
namespace {

constexpr size_t kCoordBytes = 32;
constexpr uint8_t kTagInteger = 0x02;
constexpr uint8_t kTagSequence = 0x30;

// Writes one INTEGER at `out` (which has room) and returns its encoded size.
size_t WriteInteger(const uint8_t* value, uint8_t* out) {
  size_t skip = 0;
  while (skip < kCoordBytes - 1 && value[skip] == 0) {  // NOLINT(*-pointer-arithmetic)
    ++skip;
  }
  const bool pad = (value[skip] & 0x80U) != 0;  // NOLINT(*-pointer-arithmetic)
  const size_t body = kCoordBytes - skip + (pad ? 1 : 0);
  size_t n = 0;
  out[n++] = kTagInteger;                 // NOLINT(*-pointer-arithmetic)
  out[n++] = static_cast<uint8_t>(body);  // NOLINT(*-pointer-arithmetic)
  if (pad) {
    out[n++] = 0x00;  // NOLINT(*-pointer-arithmetic)
  }
  for (size_t i = skip; i < kCoordBytes; ++i) {
    out[n++] = value[i];  // NOLINT(*-pointer-arithmetic)
  }
  return n;
}

}  // namespace

bool EcdsaRawToDer(const uint8_t* raw, uint8_t* out, size_t capacity, size_t* length) {
  *length = 0;
  if (capacity < kMaxEcdsaP256DerBytes) {
    return false;
  }
  size_t n = 2;  // the SEQUENCE header goes in front once the body length is known
  n += WriteInteger(raw, out + n);                // NOLINT(*-pointer-arithmetic)
  n += WriteInteger(raw + kCoordBytes, out + n);  // NOLINT(*-pointer-arithmetic)
  out[0] = kTagSequence;                          // NOLINT(*-pointer-arithmetic)
  out[1] = static_cast<uint8_t>(n - 2);           // at most 70, so short-form length
  *length = n;
  return true;
}

}  // namespace sarathi::core
