#include "sha256_stream.h"

namespace sarathi::ota {

Sha256Stream::Sha256Stream() {
  mbedtls_sha256_init(&ctx_);
  Restart();
}

Sha256Stream::~Sha256Stream() { mbedtls_sha256_free(&ctx_); }

void Sha256Stream::Restart() { ok_ = mbedtls_sha256_starts(&ctx_, 0) == 0; }

void Sha256Stream::Update(const uint8_t* data, size_t size) {
  ok_ = ok_ && mbedtls_sha256_update(&ctx_, data, size) == 0;
}

bool Sha256Stream::Finish(core::Sha256* out) {
  return ok_ && mbedtls_sha256_finish(&ctx_, out->data()) == 0;
}

}  // namespace sarathi::ota
