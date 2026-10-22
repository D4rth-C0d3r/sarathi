// HTTPS GET for OTA (ADR-0004 §1), on ESP-IDF's esp_http_client:
//   - servers are checked against the root CA bundle compiled into the firmware
//     (esp_crt_bundle), on every hop
//   - redirects are followed by hand so each Location can be checked: https only (or a
//     relative path on the same host), at most kMaxRedirects
//   - the body streams to a callback and stops at a size cap
// Blocking: call it from the OTA worker task, never the UI task.

#pragma once

#include <cstddef>
#include <cstdint>

namespace sarathi::ota {

constexpr int kMaxRedirects = 3;
constexpr uint32_t kHttpTimeoutMs = 15000;

enum class FetchResult : uint8_t {
  kOk,
  kConnectFailed,     // DNS, TCP or TLS (including certificate checks)
  kHttpStatus,        // a final status other than 200; see `status`
  kInsecureRedirect,  // a redirect to anything but https
  kTooManyRedirects,
  kTooLarge,  // the body grew past `max_bytes`
  kReadFailed,
  kStopped,  // the callback asked to stop
};

[[nodiscard]] const char* FetchResultName(FetchResult result);

// Called with each piece of the body. Return false to stop.
using ChunkFn = bool (*)(void* context, const uint8_t* data, size_t size);

struct FetchReport {
  int status = 0;         // last HTTP status seen
  int redirects = 0;      // hops followed
  uint64_t received = 0;  // body bytes delivered
};

[[nodiscard]] FetchResult HttpsGet(const char* url, uint64_t max_bytes, ChunkFn on_chunk,
                                   void* context, FetchReport* report);

}  // namespace sarathi::ota
