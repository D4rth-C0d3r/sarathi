// ES256 for OTA manifests with mbedTLS (ADR-0004 §3): implements core::SignatureVerifier.
// Keys are parsed once in Begin(), so verifying allocates nothing new.

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

#include <mbedtls/pk.h>

#include "ota_manifest.h"
#include "ota_trusted_keys.h"

namespace sarathi::ota {

class MbedtlsVerifier : public core::SignatureVerifier {
 public:
  static constexpr size_t kMaxKeys = 4;

  MbedtlsVerifier() = default;
  MbedtlsVerifier(const MbedtlsVerifier&) = delete;
  MbedtlsVerifier& operator=(const MbedtlsVerifier&) = delete;
  MbedtlsVerifier(MbedtlsVerifier&&) = delete;
  MbedtlsVerifier& operator=(MbedtlsVerifier&&) = delete;
  ~MbedtlsVerifier() override;

  // Parses each PEM; a key that isn't ECDSA P-256 is skipped and logged. Returns how many keys
  // are usable.
  size_t Begin(const core::TrustedKey* keys, size_t count);

  [[nodiscard]] bool Trusts(std::string_view kid) const override;
  [[nodiscard]] bool Verify(std::string_view kid, std::string_view signing_input,
                            const uint8_t* signature) const override;

 private:
  struct Slot {
    const char* kid = nullptr;
    mutable mbedtls_pk_context pk{};  // mbedtls_pk_verify takes a non-const context
  };

  [[nodiscard]] const Slot* Find(std::string_view kid) const;

  std::array<Slot, kMaxKeys> slots_{};
  size_t count_ = 0;
};

}  // namespace sarathi::ota
