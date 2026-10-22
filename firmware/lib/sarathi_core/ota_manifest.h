// OTA manifest verification, steps 1-4 and 6 of docs/ota/manifest.md. Pure logic: the ES256
// check comes in through SignatureVerifier (mbedTLS on the device, a fake in native tests),
// and the platform computes the image's SHA-256 while it downloads.

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

#include "json_reader.h"
#include "ota_types.h"

namespace sarathi::core {

class SignatureVerifier {
 public:
  SignatureVerifier() = default;
  SignatureVerifier(const SignatureVerifier&) = delete;
  SignatureVerifier& operator=(const SignatureVerifier&) = delete;
  SignatureVerifier(SignatureVerifier&&) = delete;
  SignatureVerifier& operator=(SignatureVerifier&&) = delete;
  virtual ~SignatureVerifier() = default;

  // True if `kid` names a trusted key.
  [[nodiscard]] virtual bool Trusts(std::string_view kid) const = 0;

  // ES256: true if `signature` (64 bytes, raw r || s) is valid for `signing_input` under the
  // key `kid`. Only called for trusted kids.
  [[nodiscard]] virtual bool Verify(std::string_view kid, std::string_view signing_input,
                                    const uint8_t* signature) const = 0;
};

// What the device knows about itself.
struct ManifestContext {
  std::string_view board;
  uint32_t running_seq = 0;
  uint64_t slot_size = 0;
};

// Working memory for VerifyManifest, about 7 KB: allocate it once (statically on the device).
struct ManifestScratch {
  std::array<uint8_t, kMaxManifestPartBytes> header{};
  std::array<uint8_t, kMaxManifestPartBytes> payload{};
  std::array<uint8_t, kMaxManifestPartBytes> signature{};
  JsonReader json;
};

// Applies steps 1-4 in order and returns the first failure, or kNone with `*out` filled in.
// Trailing CR and LF after the token are ignored.
[[nodiscard]] OtaError VerifyManifest(std::string_view token, const ManifestContext& context,
                                      const SignatureVerifier& verifier, ManifestScratch& scratch,
                                      Manifest* out);

// Steps 5 and 6: tracks the download against a verified manifest.
class ImageCheck {
 public:
  ImageCheck(uint64_t expected_size, const Sha256& expected_sha256)
      : expected_size_(expected_size), expected_sha256_(expected_sha256) {}

  // Counts a downloaded chunk. False once more than the manifest's size has arrived: stop the
  // download (size_mismatch).
  [[nodiscard]] bool Add(size_t bytes);

  [[nodiscard]] uint64_t Received() const { return received_; }

  // After the download, with the SHA-256 of everything received: kNone, kSizeMismatch or
  // kHashMismatch.
  [[nodiscard]] OtaError Finish(const Sha256& sha256) const;

 private:
  uint64_t expected_size_;
  Sha256 expected_sha256_;
  uint64_t received_ = 0;
};

}  // namespace sarathi::core
