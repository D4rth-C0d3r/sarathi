// The Wi-Fi OTA flow as a state machine (ADR-0004 §6). Pure logic, shared by the UI task
// (knob commands, what to show) and the OTA worker task (jobs to run, results).
//
//   Idle, up to date or Failed --Check--> Connecting --> Checking --> up to date
//                                                          \--> Available --Confirm--> Downloading
//   Downloading --> Installing --> Rebooting
//   Any failure --> Failed (with a reason). Cancel returns to Idle until Installing.
//
// Every job carries an attempt number, and the worker passes it back with each result, so a
// result that arrives after Cancel (or from an older attempt) is ignored.
//
// Not thread-safe by itself: the caller serialises access (a mutex, or a queue to the UI task).

#pragma once

#include <cstdint>

#include "ota_types.h"

namespace sarathi::core {

enum class OtaPhase : uint8_t {
  kIdle,
  kConnecting,   // joining Wi-Fi
  kChecking,     // fetching and verifying the manifest
  kUpToDate,     // the release isn't newer than what's running
  kAvailable,    // a verified newer release; waiting for the rider to confirm
  kDownloading,  // writing the inactive slot
  kInstalling,   // final checks, switching the boot partition
  kRebooting,
  kFailed,
};

enum class OtaFailure : uint8_t {
  kNone,
  kNoWifiSetup,  // no saved Wi-Fi credentials
  kWifiConnect,  // couldn't join the saved network
  kNetwork,      // the manifest couldn't be fetched
  kManifest,     // fetched but rejected; see Error()
  kDownload,     // the connection failed mid-download
  kImage,        // downloaded but rejected (size or hash); see Error()
  kFlash,        // writing or switching the partition failed
};

enum class OtaJobType : uint8_t {
  kNone,
  kConnect,        // join the saved Wi-Fi network
  kFetchManifest,  // fetch and verify the manifest
  kDownload,       // download the verified image into the inactive slot
  kInstall,        // check size and hash, then switch the boot partition
  kAbort,          // stop whatever is running, discard a partial image, Wi-Fi off
  kReboot,
};

struct OtaJob {
  OtaJobType type = OtaJobType::kNone;
  uint32_t attempt = 0;
};

class OtaUpdate {
 public:
  // ---- Knob commands (UI task). Each returns false if it doesn't apply in this phase.
  bool Check(bool wifi_configured);
  bool Confirm();
  bool Cancel();

  // ---- Worker results. Ignored unless `attempt` is the current attempt and the phase fits.
  void OnConnected(uint32_t attempt, bool ok);
  // kNone with the verified manifest's version and size; kNotNewer means up to date; any
  // other error is a rejected manifest. `fetched` false means a network failure.
  void OnManifest(uint32_t attempt, bool fetched, OtaError error, const Manifest* manifest);
  void OnProgress(uint32_t attempt, uint64_t received);
  void OnDownloaded(uint32_t attempt, bool ok);
  // The result of ImageCheck::Finish, then of switching the boot partition.
  void OnInstalled(uint32_t attempt, OtaError image_error, bool switched);

  // The next job for the worker, handed out once.
  [[nodiscard]] OtaJob TakeJob();

  // ---- State for the UI.
  [[nodiscard]] OtaPhase Phase() const { return phase_; }
  [[nodiscard]] OtaFailure Failure() const { return failure_; }
  [[nodiscard]] OtaError Error() const { return error_; }
  [[nodiscard]] const FixedString<11>& AvailableVersion() const { return version_; }
  [[nodiscard]] uint64_t AvailableSize() const { return size_; }
  [[nodiscard]] uint8_t ProgressPercent() const;
  // Bumps on every visible change, so the UI redraws only when needed.
  [[nodiscard]] uint32_t Revision() const { return revision_; }

 private:
  void Enter(OtaPhase phase, OtaJobType job);
  void Fail(OtaFailure failure, OtaError error = OtaError::kNone);
  [[nodiscard]] bool Current(uint32_t attempt, OtaPhase phase) const;

  OtaPhase phase_ = OtaPhase::kIdle;
  OtaFailure failure_ = OtaFailure::kNone;
  OtaError error_ = OtaError::kNone;
  OtaJob pending_;
  uint32_t attempt_ = 0;
  FixedString<11> version_;
  uint64_t size_ = 0;
  uint64_t received_ = 0;
  uint32_t revision_ = 0;
};

}  // namespace sarathi::core
