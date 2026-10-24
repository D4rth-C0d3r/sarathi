// First-time Wi-Fi setup through a captive portal (ADR-0004 §2) as a state machine. Pure logic:
// the UI task drives it with knob commands and platform results; the platform (soft AP, DNS,
// web page, NVS) runs the jobs it hands out.
//
//   Idle --Start--> Starting --AP up--> WaitingForPhone --form sent--> Joining
//   Joining --joined--> Joined (saved; the AP stays up briefly so the phone sees it worked)
//   Joining --failed--> WaitingForPhone (try again; the reason is shown)
//   Starting/WaitingForPhone/Joining --5 min--> Failed (timed out)
//   Cancel (long press) returns to Idle from any active phase.

#pragma once

#include <cstdint>
#include <string_view>

#include "fixed_string.h"
#include "wifi_credentials.h"

namespace sarathi::core {

constexpr const char* kSetupApName = "sarathi-setup";
constexpr size_t kSetupPasswordLength = 8;
constexpr uint32_t kWifiSetupTimeoutMs = 5U * 60U * 1000U;
constexpr uint32_t kJoinedLingerMs = 3000;  // AP up after success, for the phone's page

enum class WifiSetupPhase : uint8_t {
  kIdle,
  kStarting,         // starting the access point
  kWaitingForPhone,  // AP up: join it and fill in the page
  kJoining,          // trying the network the phone sent
  kJoined,           // joined and saved
  kFailed,
};

enum class WifiSetupFailure : uint8_t { kNone, kAccessPoint, kTimedOut };

enum class WifiSetupJob : uint8_t {
  kNone,
  kStartAccessPoint,  // with Password()
  kStopAccessPoint,   // and switch the radio off
  kForget,            // erase the saved network, then SetSaved("")
};

class WifiSetup {
 public:
  // ---- Knob commands (UI task). Each returns false if it doesn't apply now.
  bool Start(uint32_t now_ms, std::string_view password);
  bool Cancel();
  // Leaves Joined or Failed (the screen closes).
  bool Close();
  bool Forget();

  // ---- Platform results.
  void OnAccessPointStarted(bool ok);
  void OnSubmitted(std::string_view ssid);
  void OnJoinResult(bool ok);
  void SetSaved(std::string_view ssid);  // empty: no network saved
  void Tick(uint32_t now_ms);

  [[nodiscard]] WifiSetupJob TakeJob();

  // ---- State for the UI and the web page.
  [[nodiscard]] WifiSetupPhase Phase() const { return phase_; }
  [[nodiscard]] WifiSetupFailure Failure() const { return failure_; }
  [[nodiscard]] bool Active() const;  // the access point is (being) up
  [[nodiscard]] std::string_view Password() const { return password_.View(); }
  [[nodiscard]] std::string_view JoiningSsid() const { return joining_.View(); }
  [[nodiscard]] bool LastJoinFailed() const { return last_join_failed_; }
  [[nodiscard]] bool HasSaved() const { return !saved_.Empty(); }
  [[nodiscard]] std::string_view SavedSsid() const { return saved_.View(); }
  [[nodiscard]] uint32_t Revision() const { return revision_; }

 private:
  void Enter(WifiSetupPhase phase);
  void Queue(WifiSetupJob job) { job_ = job; }

  WifiSetupPhase phase_ = WifiSetupPhase::kIdle;
  WifiSetupFailure failure_ = WifiSetupFailure::kNone;
  WifiSetupJob job_ = WifiSetupJob::kNone;
  FixedString<kSetupPasswordLength> password_;
  FixedString<kMaxSsidBytes> joining_;
  FixedString<kMaxSsidBytes> saved_;
  bool last_join_failed_ = false;
  bool ap_up_ = false;
  uint32_t started_ms_ = 0;
  uint32_t joined_ms_ = 0;
  uint32_t now_ms_ = 0;
  uint32_t revision_ = 0;
};

}  // namespace sarathi::core
