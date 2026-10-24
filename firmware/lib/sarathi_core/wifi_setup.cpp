#include "wifi_setup.h"

namespace sarathi::core {
namespace {

// Time since `since`, never negative: a caller's `now` can be a little older than the moment
// a phase began (read at the top of the loop, before the knob started setup).
uint32_t Elapsed(uint32_t now_ms, uint32_t since_ms) {
  const auto diff = static_cast<int32_t>(now_ms - since_ms);
  return diff > 0 ? static_cast<uint32_t>(diff) : 0;
}

}  // namespace

void WifiSetup::Enter(WifiSetupPhase phase) {
  phase_ = phase;
  ++revision_;
}

bool WifiSetup::Active() const {
  return phase_ == WifiSetupPhase::kStarting || phase_ == WifiSetupPhase::kWaitingForPhone ||
         phase_ == WifiSetupPhase::kJoining || (phase_ == WifiSetupPhase::kJoined && ap_up_);
}

bool WifiSetup::Start(uint32_t now_ms, std::string_view password) {
  if (Active() || password.size() != kSetupPasswordLength) {
    return false;
  }
  password_.Assign(password);
  joining_.Assign("");
  failure_ = WifiSetupFailure::kNone;
  last_join_failed_ = false;
  started_ms_ = now_ms;
  now_ms_ = now_ms;
  Queue(WifiSetupJob::kStartAccessPoint);
  Enter(WifiSetupPhase::kStarting);
  return true;
}

bool WifiSetup::Cancel() {
  if (!Active()) {
    return false;
  }
  Queue(WifiSetupJob::kStopAccessPoint);
  ap_up_ = false;
  Enter(WifiSetupPhase::kIdle);
  return true;
}

bool WifiSetup::Close() {
  if (phase_ != WifiSetupPhase::kJoined && phase_ != WifiSetupPhase::kFailed) {
    return false;
  }
  if (ap_up_) {
    Queue(WifiSetupJob::kStopAccessPoint);
    ap_up_ = false;
  }
  Enter(WifiSetupPhase::kIdle);
  return true;
}

bool WifiSetup::Forget() {
  if (Active() || saved_.Empty()) {
    return false;
  }
  Queue(WifiSetupJob::kForget);
  return true;
}

void WifiSetup::OnAccessPointStarted(bool ok) {
  if (phase_ != WifiSetupPhase::kStarting) {
    return;
  }
  ap_up_ = ok;
  if (ok) {
    Enter(WifiSetupPhase::kWaitingForPhone);
  } else {
    failure_ = WifiSetupFailure::kAccessPoint;
    Queue(WifiSetupJob::kStopAccessPoint);
    Enter(WifiSetupPhase::kFailed);
  }
}

void WifiSetup::OnSubmitted(std::string_view ssid) {
  if (phase_ != WifiSetupPhase::kWaitingForPhone) {
    return;
  }
  joining_.Assign(ssid);
  last_join_failed_ = false;
  Enter(WifiSetupPhase::kJoining);
}

void WifiSetup::OnJoinResult(bool ok) {
  if (phase_ != WifiSetupPhase::kJoining) {
    return;
  }
  if (ok) {
    saved_ = joining_;
    joined_ms_ = now_ms_;
    Enter(WifiSetupPhase::kJoined);
  } else {
    last_join_failed_ = true;
    Enter(WifiSetupPhase::kWaitingForPhone);
  }
}

void WifiSetup::SetSaved(std::string_view ssid) {
  saved_.Assign(ssid);
  ++revision_;
}

void WifiSetup::Tick(uint32_t now_ms) {
  now_ms_ = now_ms;
  const bool waiting = phase_ == WifiSetupPhase::kStarting ||
                       phase_ == WifiSetupPhase::kWaitingForPhone ||
                       phase_ == WifiSetupPhase::kJoining;
  if (waiting && Elapsed(now_ms, started_ms_) >= kWifiSetupTimeoutMs) {
    failure_ = WifiSetupFailure::kTimedOut;
    Queue(WifiSetupJob::kStopAccessPoint);
    ap_up_ = false;
    Enter(WifiSetupPhase::kFailed);
  } else if (phase_ == WifiSetupPhase::kJoined && ap_up_ &&
             Elapsed(now_ms, joined_ms_) >= kJoinedLingerMs) {
    Queue(WifiSetupJob::kStopAccessPoint);
    ap_up_ = false;
  }
}

WifiSetupJob WifiSetup::TakeJob() {
  const WifiSetupJob job = job_;
  job_ = WifiSetupJob::kNone;
  return job;
}

}  // namespace sarathi::core
