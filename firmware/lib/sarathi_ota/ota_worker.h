// The OTA worker task (ADR-0004 §6). core::OtaUpdate stays on the UI task; it hands jobs to
// the worker through a queue and applies the worker's results from another queue, so the
// state machine and LVGL never run on two tasks.
//
//   UI task:     update.Check(...) -> worker.Post(update.TakeJob())
//                worker.TakeResult(&r) -> ApplyResult(r, update)
//   OTA worker:  Wi-Fi, HTTPS, manifest verification, flash writes (blocking work)

#pragma once

#include <cstdint>

#include "fixed_string.h"
#include "ota_types.h"
#include "ota_update.h"
#include "wifi_credentials.h"

namespace sarathi::ota {

// The device class in manifests (docs/ota/manifest.md).
constexpr const char* kBoardId = "crowpanel21";

// Where releases are found. Test builds may point it elsewhere.
#ifndef SARATHI_OTA_MANIFEST_URL
#define SARATHI_OTA_MANIFEST_URL \
  "https://github.com/D4rth-C0d3r/sarathi/releases/latest/download/manifest.jws"
#endif

struct OtaResult {
  enum class Kind : uint8_t { kConnected, kManifest, kProgress, kDownloaded, kInstalled };

  Kind kind = Kind::kConnected;
  uint32_t attempt = 0;
  bool ok = false;  // connected / fetched / downloaded / switched
  core::OtaError error = core::OtaError::kNone;
  uint64_t bytes = 0;  // progress: received so far; manifest: image size
  core::FixedString<11> version;
  bool weak_signal = false;  // connected / downloaded: Wi-Fi below core::kMinUpdateRssiDbm
};

// Reads the saved network; false if none is saved.
using CredentialsFn = bool (*)(core::WifiCredentials* out);

class OtaWorker {
 public:
  // Starts the task (core 0, low priority). Returns false if it couldn't be created.
  bool Begin(CredentialsFn credentials);

  // Non-blocking. Jobs of type kNone are ignored.
  void Post(const core::OtaJob& job);

  // Non-blocking. False when no result is waiting.
  bool TakeResult(OtaResult* out);
};

// Feeds a worker result into the state machine (UI task).
void ApplyResult(const OtaResult& result, core::OtaUpdate& update);

}  // namespace sarathi::ota
