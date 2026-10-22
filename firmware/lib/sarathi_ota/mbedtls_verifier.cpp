#include "mbedtls_verifier.h"

#include <cstring>

#include <esp_log.h>
#include <mbedtls/sha256.h>

#include "ecdsa_der.h"

namespace sarathi::ota {
namespace {

constexpr const char* kTag = "ota_verify";
constexpr size_t kP256Bits = 256;

}  // namespace

MbedtlsVerifier::~MbedtlsVerifier() {
  for (size_t i = 0; i < count_; ++i) {
    mbedtls_pk_free(&slots_.at(i).pk);
  }
}

size_t MbedtlsVerifier::Begin(const core::TrustedKey* keys, size_t count) {
  for (size_t i = 0; i < count && count_ < kMaxKeys; ++i) {
    const core::TrustedKey& key = keys[i];  // NOLINT(*-pointer-arithmetic)
    Slot& slot = slots_.at(count_);
    mbedtls_pk_init(&slot.pk);
    // PEM input must include the terminating NUL in its length.
    const int err = mbedtls_pk_parse_public_key(
        &slot.pk, reinterpret_cast<const unsigned char*>(key.public_key_pem),  // NOLINT
        std::strlen(key.public_key_pem) + 1);
    if (err != 0 || mbedtls_pk_get_type(&slot.pk) != MBEDTLS_PK_ECKEY ||
        mbedtls_pk_get_bitlen(&slot.pk) != kP256Bits) {
      ESP_LOGE(kTag, "key '%s' is not a usable ECDSA P-256 key (err -0x%04x)", key.kid, -err);
      mbedtls_pk_free(&slot.pk);
      continue;
    }
    slot.kid = key.kid;
    ++count_;
  }
  ESP_LOGI(kTag, "%u trusted OTA key(s)", static_cast<unsigned>(count_));
  return count_;
}

const MbedtlsVerifier::Slot* MbedtlsVerifier::Find(std::string_view kid) const {
  for (size_t i = 0; i < count_; ++i) {
    if (kid == slots_.at(i).kid) {
      return &slots_.at(i);
    }
  }
  return nullptr;
}

bool MbedtlsVerifier::Trusts(std::string_view kid) const { return Find(kid) != nullptr; }

bool MbedtlsVerifier::Verify(std::string_view kid, std::string_view signing_input,
                             const uint8_t* signature) const {
  const Slot* slot = Find(kid);
  if (slot == nullptr) {
    return false;
  }
  std::array<uint8_t, core::kSha256Bytes> hash{};
  if (mbedtls_sha256(reinterpret_cast<const unsigned char*>(signing_input.data()),  // NOLINT
                     signing_input.size(), hash.data(), 0) != 0) {
    return false;
  }
  std::array<uint8_t, core::kMaxEcdsaP256DerBytes> der{};
  size_t der_size = 0;
  if (!core::EcdsaRawToDer(signature, der.data(), der.size(), &der_size)) {
    return false;
  }
  return mbedtls_pk_verify(&slot->pk, MBEDTLS_MD_SHA256, hash.data(), hash.size(), der.data(),
                           der_size) == 0;
}

}  // namespace sarathi::ota
