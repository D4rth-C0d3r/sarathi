// Public keys the firmware trusts for OTA manifests, by kid (ADR-0004, docs/ota/keys/). Each
// PEM must match its file in docs/ota/keys byte for byte (test_ota_manifest checks). To rotate,
// add the new key here first and ship a release that trusts both.

#pragma once

#include <array>

namespace sarathi::core {

struct TrustedKey {
  const char* kid;
  const char* public_key_pem;  // SubjectPublicKeyInfo, ECDSA P-256
};

inline constexpr std::array<TrustedKey, 1> kTrustedOtaKeys = {{
    {"sarathi-ota-1",
     "-----BEGIN PUBLIC KEY-----\n"
     "MFkwEwYHKoZIzj0CAQYIKoZIzj0DAQcDQgAEVCjp2bOg6QsyGV+uHgoTj8Ydxz8n\n"
     "o79JPu2862IeO381XVZBrvAfXztwSee/zO75W6qhS7QpBVi/nzRl5Dky0A==\n"
     "-----END PUBLIC KEY-----\n"},
}};

}  // namespace sarathi::core
