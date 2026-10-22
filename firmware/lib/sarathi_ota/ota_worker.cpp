#include "ota_worker.h"

#include <array>
#include <cstring>
#include <optional>

#include <esp_log.h>
#include <esp_ota_ops.h>
#include <esp_system.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>

#include "https_get.h"
#include "mbedtls_verifier.h"
#include "ota_manifest.h"
#include "ota_trusted_keys.h"
#include "sha256_stream.h"
#include "version.h"
#include "wifi_link.h"

namespace sarathi::ota {
namespace {

constexpr const char* kTag = "ota";
constexpr uint32_t kStackBytes = 12288;  // TLS handshakes run on this stack
constexpr UBaseType_t kPriority = 1;
constexpr BaseType_t kCore = 0;  // with the Wi-Fi stack; the UI runs on core 1
constexpr UBaseType_t kQueueLength = 8;
constexpr uint32_t kRebootDelayMs = 300;

// Everything the worker owns. Allocated once, statically; only the worker task touches it.
struct Worker {
  QueueHandle_t jobs = nullptr;
  QueueHandle_t results = nullptr;
  CredentialsFn credentials = nullptr;
  MbedtlsVerifier verifier;
  core::ManifestScratch scratch;
  core::Manifest manifest;
  std::array<char, core::kMaxManifestBytes + 3> body{};  // the token, plus CR LF and NUL
  size_t body_size = 0;
  const esp_partition_t* partition = nullptr;
  esp_ota_handle_t handle = 0;
  std::optional<core::ImageCheck> check;
  Sha256Stream sha;
  bool flash_error = false;
  bool overrun = false;
  bool abort_requested = false;
  uint8_t last_percent = 0;
  uint32_t attempt = 0;
};

Worker* worker = nullptr;  // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)

void Send(OtaResult result) { xQueueSend(worker->results, &result, 0); }

OtaResult Result(OtaResult::Kind kind, bool ok) {
  OtaResult r;
  r.kind = kind;
  r.attempt = worker->attempt;
  r.ok = ok;
  return r;
}

bool AbortWaiting() {
  core::OtaJob next;
  return xQueuePeek(worker->jobs, &next, 0) == pdTRUE && next.type == core::OtaJobType::kAbort;
}

bool CollectBody(void* /*context*/, const uint8_t* data, size_t size) {
  if (worker->body_size + size >= worker->body.size()) {
    return false;
  }
  std::memcpy(&worker->body.at(worker->body_size), data, size);
  worker->body_size += size;
  return true;
}

bool WriteImage(void* /*context*/, const uint8_t* data, size_t size) {
  if (AbortWaiting()) {
    worker->abort_requested = true;
    return false;
  }
  if (!worker->check->Add(size)) {
    worker->overrun = true;  // reported by the install step as size_mismatch
    return false;
  }
  worker->sha.Update(data, size);
  if (esp_ota_write(worker->handle, data, size) != ESP_OK) {
    worker->flash_error = true;
    return false;
  }
  const uint64_t received = worker->check->Received();
  const auto percent = static_cast<uint8_t>(received * 100 / worker->manifest.size);
  if (percent != worker->last_percent) {  // one message per percent, not per chunk
    worker->last_percent = percent;
    OtaResult r = Result(OtaResult::Kind::kProgress, true);
    r.bytes = received;
    Send(r);
  }
  return true;
}

void ReleaseImage() {
  if (worker->handle != 0) {
    esp_ota_abort(worker->handle);
    worker->handle = 0;
  }
  worker->check.reset();
}

void DoConnect() {
  core::WifiCredentials credentials;
  const bool ok = worker->credentials != nullptr && worker->credentials(&credentials) &&
                  WifiConnect(credentials);
  Send(Result(OtaResult::Kind::kConnected, ok));
}

void DoFetchManifest() {
  OtaResult r = Result(OtaResult::Kind::kManifest, false);
  worker->body_size = 0;
  FetchReport report;
  const FetchResult fetched =
      HttpsGet(SARATHI_OTA_MANIFEST_URL, worker->body.size() - 1, CollectBody, nullptr, &report);
  if (fetched != FetchResult::kOk) {
    ESP_LOGW(kTag, "manifest fetch failed: %s (HTTP %d)", FetchResultName(fetched), report.status);
    Send(r);
    return;
  }
  r.ok = true;
  worker->partition = esp_ota_get_next_update_partition(nullptr);
  const core::ManifestContext context{kBoardId, core::kFirmwareSeq,
                                      worker->partition != nullptr ? worker->partition->size : 0};
  r.error = core::VerifyManifest({worker->body.data(), worker->body_size}, context,
                                 worker->verifier, worker->scratch, &worker->manifest);
  ESP_LOGI(kTag, "manifest: %s%s%s", core::OtaErrorCode(r.error),
           r.error == core::OtaError::kNone ? ", version " : "",
           r.error == core::OtaError::kNone ? worker->manifest.version.CStr() : "");
  if (r.error == core::OtaError::kNone) {
    r.version = worker->manifest.version;
    r.bytes = worker->manifest.size;
  }
  Send(r);
}

void DoDownload() {
  ReleaseImage();
  worker->flash_error = false;
  worker->overrun = false;
  worker->abort_requested = false;
  worker->last_percent = 0;
  worker->sha.Restart();
  worker->check.emplace(worker->manifest.size, worker->manifest.sha256);
  // Sequential writes erase each sector just before writing it, instead of erasing the whole
  // slot up front (a multi-second flash stall).
  if (worker->partition == nullptr ||
      esp_ota_begin(worker->partition, OTA_WITH_SEQUENTIAL_WRITES, &worker->handle) != ESP_OK) {
    ESP_LOGE(kTag, "esp_ota_begin failed");
    Send(Result(OtaResult::Kind::kDownloaded, false));
    return;
  }
  FetchReport report;
  // No size cap here: ImageCheck stops the download as soon as it overruns the manifest's size.
  const FetchResult fetched =
      HttpsGet(worker->manifest.url.CStr(), UINT64_MAX, WriteImage, nullptr, &report);
  const bool ok =
      fetched == FetchResult::kOk || (fetched == FetchResult::kStopped && worker->overrun);
  if (worker->flash_error) {
    ESP_LOGE(kTag, "flash write failed");
  }
  if (!ok && !worker->abort_requested) {
    ESP_LOGW(kTag, "download failed: %s (HTTP %d)", FetchResultName(fetched), report.status);
  }
  Send(Result(OtaResult::Kind::kDownloaded, ok));
}

void DoInstall() {
  OtaResult r = Result(OtaResult::Kind::kInstalled, false);
  core::Sha256 digest{};
  if (!worker->check.has_value() || !worker->sha.Finish(&digest)) {
    r.error = core::OtaError::kHashMismatch;
  } else {
    r.error = worker->check->Finish(digest);
  }
  if (r.error != core::OtaError::kNone) {
    ESP_LOGW(kTag, "image rejected: %s", core::OtaErrorCode(r.error));
    ReleaseImage();
    Send(r);
    return;
  }
  // esp_ota_end validates the image format; only then does the boot partition change.
  esp_err_t err = esp_ota_end(worker->handle);
  worker->handle = 0;
  worker->check.reset();
  if (err == ESP_OK) {
    err = esp_ota_set_boot_partition(worker->partition);
  }
  r.ok = err == ESP_OK;
  if (r.ok) {
    ESP_LOGI(kTag, "installed %s into %s", worker->manifest.version.CStr(),
             worker->partition->label);
  } else {
    ESP_LOGE(kTag, "install failed: %s", esp_err_to_name(err));
  }
  Send(r);
}

void TaskMain(void* /*arg*/) {
  worker->verifier.Begin(core::kTrustedOtaKeys.data(), core::kTrustedOtaKeys.size());
  for (;;) {
    core::OtaJob job;
    if (xQueueReceive(worker->jobs, &job, portMAX_DELAY) != pdTRUE) {
      continue;
    }
    worker->attempt = job.attempt;
    switch (job.type) {
      case core::OtaJobType::kConnect:
        DoConnect();
        break;
      case core::OtaJobType::kFetchManifest:
        DoFetchManifest();
        break;
      case core::OtaJobType::kDownload:
        DoDownload();
        break;
      case core::OtaJobType::kInstall:
        DoInstall();
        break;
      case core::OtaJobType::kAbort:
        ReleaseImage();
        WifiOff();
        break;
      case core::OtaJobType::kReboot:
        WifiOff();
        vTaskDelay(pdMS_TO_TICKS(kRebootDelayMs));  // let the log line out
        esp_restart();
        break;
      case core::OtaJobType::kNone:
        break;
    }
  }
}

}  // namespace

bool OtaWorker::Begin(CredentialsFn credentials) {
  if (worker != nullptr) {
    return true;
  }
  worker = new Worker();  // NOLINT(cppcoreguidelines-owning-memory): once, at init, never freed
  worker->credentials = credentials;
  worker->jobs = xQueueCreate(kQueueLength, sizeof(core::OtaJob));
  worker->results = xQueueCreate(kQueueLength, sizeof(OtaResult));
  if (worker->jobs == nullptr || worker->results == nullptr ||
      xTaskCreatePinnedToCore(TaskMain, "ota", kStackBytes, nullptr, kPriority, nullptr, kCore) !=
          pdPASS) {
    ESP_LOGE(kTag, "could not start the OTA worker");
    return false;
  }
  return true;
}

void OtaWorker::Post(const core::OtaJob& job) {
  if (worker != nullptr && job.type != core::OtaJobType::kNone) {
    xQueueSend(worker->jobs, &job, 0);
  }
}

bool OtaWorker::TakeResult(OtaResult* out) {
  return worker != nullptr && xQueueReceive(worker->results, out, 0) == pdTRUE;
}

void ApplyResult(const OtaResult& result, core::OtaUpdate& update) {
  switch (result.kind) {
    case OtaResult::Kind::kConnected:
      update.OnConnected(result.attempt, result.ok);
      break;
    case OtaResult::Kind::kManifest: {
      core::Manifest summary;  // the state machine only needs what the UI shows
      summary.version = result.version;
      summary.size = result.bytes;
      update.OnManifest(result.attempt, result.ok, result.error, &summary);
      break;
    }
    case OtaResult::Kind::kProgress:
      update.OnProgress(result.attempt, result.bytes);
      break;
    case OtaResult::Kind::kDownloaded:
      update.OnDownloaded(result.attempt, result.ok);
      break;
    case OtaResult::Kind::kInstalled:
      update.OnInstalled(result.attempt, result.error, result.ok);
      break;
  }
}

}  // namespace sarathi::ota
