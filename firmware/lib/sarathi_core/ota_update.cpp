#include "ota_update.h"

namespace sarathi::core {

void OtaUpdate::Enter(OtaPhase phase, OtaJobType job) {
  phase_ = phase;
  if (job != OtaJobType::kNone) {
    pending_ = OtaJob{job, attempt_};
  }
  ++revision_;
}

void OtaUpdate::Fail(OtaFailure failure, OtaError error) {
  failure_ = failure;
  error_ = error;
  Enter(OtaPhase::kFailed, OtaJobType::kAbort);  // release the partial image and Wi-Fi
}

bool OtaUpdate::Current(uint32_t attempt, OtaPhase phase) const {
  return attempt == attempt_ && phase == phase_;
}

bool OtaUpdate::Check(bool wifi_configured) {
  if (phase_ != OtaPhase::kIdle && phase_ != OtaPhase::kUpToDate && phase_ != OtaPhase::kFailed) {
    return false;
  }
  ++attempt_;
  failure_ = OtaFailure::kNone;
  error_ = OtaError::kNone;
  received_ = 0;
  if (!wifi_configured) {
    Fail(OtaFailure::kNoWifiSetup);
    pending_ = OtaJob{};  // nothing was started
    return true;
  }
  Enter(OtaPhase::kConnecting, OtaJobType::kConnect);
  return true;
}

bool OtaUpdate::Confirm() {
  if (phase_ != OtaPhase::kAvailable) {
    return false;
  }
  received_ = 0;
  Enter(OtaPhase::kDownloading, OtaJobType::kDownload);
  return true;
}

bool OtaUpdate::Cancel() {
  switch (phase_) {
    case OtaPhase::kConnecting:
    case OtaPhase::kChecking:
    case OtaPhase::kAvailable:
    case OtaPhase::kDownloading:
      Enter(OtaPhase::kIdle, OtaJobType::kAbort);
      ++attempt_;  // anything still in flight now belongs to an old attempt
      return true;
    default:
      return false;  // nothing to cancel, or too late (Installing, Rebooting)
  }
}

void OtaUpdate::OnConnected(uint32_t attempt, bool ok) {
  if (!Current(attempt, OtaPhase::kConnecting)) {
    return;
  }
  if (ok) {
    Enter(OtaPhase::kChecking, OtaJobType::kFetchManifest);
  } else {
    Fail(OtaFailure::kWifiConnect);
  }
}

void OtaUpdate::OnManifest(uint32_t attempt, bool fetched, OtaError error,
                           const Manifest* manifest) {
  if (!Current(attempt, OtaPhase::kChecking)) {
    return;
  }
  if (!fetched) {
    Fail(OtaFailure::kNetwork);
  } else if (error == OtaError::kNotNewer) {
    Enter(OtaPhase::kUpToDate, OtaJobType::kAbort);  // nothing to do: Wi-Fi off
  } else if (error != OtaError::kNone || manifest == nullptr) {
    Fail(OtaFailure::kManifest, error);
  } else {
    version_ = manifest->version;
    size_ = manifest->size;
    Enter(OtaPhase::kAvailable, OtaJobType::kNone);
  }
}

void OtaUpdate::OnProgress(uint32_t attempt, uint64_t received) {
  if (!Current(attempt, OtaPhase::kDownloading)) {
    return;
  }
  const uint8_t before = ProgressPercent();
  received_ = received;
  if (ProgressPercent() != before) {
    ++revision_;  // redraw per whole percent, not per chunk
  }
}

void OtaUpdate::OnDownloaded(uint32_t attempt, bool ok) {
  if (!Current(attempt, OtaPhase::kDownloading)) {
    return;
  }
  if (ok) {
    Enter(OtaPhase::kInstalling, OtaJobType::kInstall);
  } else {
    Fail(OtaFailure::kDownload);
  }
}

void OtaUpdate::OnInstalled(uint32_t attempt, OtaError image_error, bool switched) {
  if (!Current(attempt, OtaPhase::kInstalling)) {
    return;
  }
  if (image_error != OtaError::kNone) {
    Fail(OtaFailure::kImage, image_error);
  } else if (!switched) {
    Fail(OtaFailure::kFlash);
  } else {
    Enter(OtaPhase::kRebooting, OtaJobType::kReboot);
  }
}

OtaJob OtaUpdate::TakeJob() {
  const OtaJob job = pending_;
  pending_ = OtaJob{};
  return job;
}

uint8_t OtaUpdate::ProgressPercent() const {
  if (size_ == 0) {
    return 0;
  }
  const uint64_t received = received_ < size_ ? received_ : size_;
  return static_cast<uint8_t>(received * 100 / size_);
}

}  // namespace sarathi::core
