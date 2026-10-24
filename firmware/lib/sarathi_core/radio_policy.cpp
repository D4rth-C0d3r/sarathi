#include "radio_policy.h"

namespace sarathi::core {

bool WifiInUse(OtaPhase ota, WifiSetupPhase setup) {
  switch (ota) {
    case OtaPhase::kConnecting:
    case OtaPhase::kChecking:
    case OtaPhase::kAvailable:
    case OtaPhase::kDownloading:
    case OtaPhase::kInstalling:
    case OtaPhase::kRebooting:
      return true;
    case OtaPhase::kIdle:
    case OtaPhase::kUpToDate:
    case OtaPhase::kFailed:
      break;
  }
  switch (setup) {
    case WifiSetupPhase::kStarting:
    case WifiSetupPhase::kWaitingForPhone:
    case WifiSetupPhase::kJoining:
    case WifiSetupPhase::kJoined:
      return true;
    case WifiSetupPhase::kIdle:
    case WifiSetupPhase::kFailed:
      break;
  }
  return false;
}

bool RadioPolicy::Update(bool wifi_in_use, uint32_t now_ms) {
  if (wifi_in_use) {
    paused_ = true;
    wifi_off_since_ms_ = now_ms;
  } else if (paused_ && now_ms - wifi_off_since_ms_ >= kBleResumeDelayMs) {
    paused_ = false;
  }
  return paused_;
}

}  // namespace sarathi::core
