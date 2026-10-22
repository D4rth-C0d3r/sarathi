#include "https_get.h"

#include <array>
#include <cstdio>
#include <cstring>

#include <esp_crt_bundle.h>
#include <esp_http_client.h>
#include <esp_log.h>
#include <strings.h>

#include "version.h"

namespace sarathi::ota {
namespace {

constexpr const char* kTag = "https";
// GitHub's signed asset URLs run to about 900 characters.
constexpr size_t kMaxUrl = 2048;
constexpr int kRxBufferBytes = 4096;
constexpr int kTxBufferBytes = 2560;  // the request line carries the long signed URL
constexpr size_t kChunkBytes = 4096;

struct RequestState {
  std::array<char, kMaxUrl> location{};
  bool location_too_long = false;
};

// One request at a time (the OTA worker), so these live here instead of on its stack.
// NOLINTBEGIN(cppcoreguidelines-avoid-non-const-global-variables)
RequestState state;
std::array<char, kMaxUrl> current_url{};
std::array<uint8_t, kChunkBytes> chunk{};
// NOLINTEND(cppcoreguidelines-avoid-non-const-global-variables)

esp_err_t OnEvent(esp_http_client_event_t* event) {
  if (event->event_id == HTTP_EVENT_ON_HEADER && strcasecmp(event->header_key, "Location") == 0) {
    auto* s = static_cast<RequestState*>(event->user_data);
    const size_t n = std::strlen(event->header_value);
    s->location_too_long = n >= s->location.size();
    if (!s->location_too_long) {
      std::memcpy(s->location.data(), event->header_value, n + 1);
    }
  }
  return ESP_OK;
}

bool IsRedirect(int status) {
  return status == 301 || status == 302 || status == 303 || status == 307 || status == 308;
}

// Absolute https URLs, or a path on the current (https) host.
bool SafeLocation(const char* location) {
  return std::strncmp(location, "https://", 8) == 0 ||
         (location[0] == '/' && location[1] != '/');  // "//host" would switch hosts
}

}  // namespace

const char* FetchResultName(FetchResult result) {
  switch (result) {
    case FetchResult::kOk:
      return "ok";
    case FetchResult::kConnectFailed:
      return "connect failed";
    case FetchResult::kHttpStatus:
      return "HTTP status";
    case FetchResult::kInsecureRedirect:
      return "insecure redirect";
    case FetchResult::kTooManyRedirects:
      return "too many redirects";
    case FetchResult::kTooLarge:
      return "too large";
    case FetchResult::kReadFailed:
      return "read failed";
    case FetchResult::kStopped:
      return "stopped";
  }
  return "?";
}

FetchResult HttpsGet(const char* url, uint64_t max_bytes, ChunkFn on_chunk, void* context,
                     FetchReport* report) {
  *report = FetchReport{};
  if (std::strncmp(url, "https://", 8) != 0 || std::strlen(url) >= current_url.size()) {
    return FetchResult::kInsecureRedirect;
  }
  std::strncpy(current_url.data(), url, current_url.size() - 1);

  std::array<char, 32> agent{};
  std::snprintf(agent.data(), agent.size(), "sarathi/%.*s",
                static_cast<int>(core::kFirmwareVersion.size()), core::kFirmwareVersion.data());
  esp_http_client_config_t config{};
  config.url = current_url.data();
  config.crt_bundle_attach = esp_crt_bundle_attach;
  config.disable_auto_redirect = true;
  config.timeout_ms = static_cast<int>(kHttpTimeoutMs);
  config.buffer_size = kRxBufferBytes;
  config.buffer_size_tx = kTxBufferBytes;
  config.user_agent = agent.data();
  config.event_handler = OnEvent;
  config.user_data = &state;
  esp_http_client_handle_t client = esp_http_client_init(&config);
  if (client == nullptr) {
    return FetchResult::kConnectFailed;
  }

  FetchResult result = FetchResult::kOk;
  for (;;) {
    state.location[0] = '\0';
    state.location_too_long = false;
    if (esp_http_client_open(client, 0) != ESP_OK) {
      result = FetchResult::kConnectFailed;
      break;
    }
    const int64_t length = esp_http_client_fetch_headers(client);
    report->status = esp_http_client_get_status_code(client);
    if (IsRedirect(report->status)) {
      esp_http_client_close(client);
      if (report->redirects >= kMaxRedirects) {
        result = FetchResult::kTooManyRedirects;
        break;
      }
      if (state.location_too_long || state.location[0] == '\0' ||
          !SafeLocation(state.location.data())) {
        ESP_LOGW(kTag, "refusing redirect (status %d)", report->status);
        result = FetchResult::kInsecureRedirect;
        break;
      }
      ++report->redirects;
      esp_http_client_set_url(client, state.location.data());
      continue;
    }
    if (report->status != 200) {
      result = FetchResult::kHttpStatus;
      break;
    }
    if (length > 0 && static_cast<uint64_t>(length) > max_bytes) {
      result = FetchResult::kTooLarge;
      break;
    }
    for (;;) {
      const int n = esp_http_client_read(client, reinterpret_cast<char*>(chunk.data()),  // NOLINT
                                         static_cast<int>(chunk.size()));
      if (n < 0) {
        result = FetchResult::kReadFailed;
        break;
      }
      if (n == 0) {
        if (!esp_http_client_is_complete_data_received(client)) {
          result = FetchResult::kReadFailed;  // the connection ended early
        }
        break;
      }
      report->received += static_cast<uint64_t>(n);
      if (report->received > max_bytes) {
        result = FetchResult::kTooLarge;
        break;
      }
      if (!on_chunk(context, chunk.data(), static_cast<size_t>(n))) {
        result = FetchResult::kStopped;
        break;
      }
    }
    break;
  }
  esp_http_client_close(client);
  esp_http_client_cleanup(client);
  ESP_LOGI(kTag, "GET done: %s, status %d, %d redirect(s), %llu B", FetchResultName(result),
           report->status, report->redirects, static_cast<unsigned long long>(report->received));
  return result;
}

}  // namespace sarathi::ota
