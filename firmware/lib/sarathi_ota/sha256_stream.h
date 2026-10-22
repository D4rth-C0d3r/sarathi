// SHA-256 over a download, chunk by chunk (mbedTLS, hardware-accelerated on the ESP32-S3).

#pragma once

#include <cstddef>
#include <cstdint>

#include <mbedtls/sha256.h>

#include "ota_types.h"

namespace sarathi::ota {

class Sha256Stream {
 public:
  Sha256Stream();
  Sha256Stream(const Sha256Stream&) = delete;
  Sha256Stream& operator=(const Sha256Stream&) = delete;
  Sha256Stream(Sha256Stream&&) = delete;
  Sha256Stream& operator=(Sha256Stream&&) = delete;
  ~Sha256Stream();

  void Restart();
  void Update(const uint8_t* data, size_t size);
  // The digest of everything since Restart(). Returns false if mbedTLS failed at any point.
  bool Finish(core::Sha256* out);

 private:
  mbedtls_sha256_context ctx_{};
  bool ok_ = false;
};

}  // namespace sarathi::ota
