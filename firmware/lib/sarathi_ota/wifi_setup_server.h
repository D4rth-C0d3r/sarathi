// The captive portal behind core::WifiSetup (ADR-0004 §2): a WPA2 soft access point, a DNS
// responder that sends every name to the device, and a small web page (ESP-IDF
// esp_http_server) that takes the network name and password.
//
// The web server runs on its own task; it only parses requests and swaps data with Poll()
// under a lock. Poll() runs on the UI task: it answers DNS, collects scan results, and runs
// the join attempt without blocking (Wi-Fi joins asynchronously).

#pragma once

#include <cstdint>
#include <string_view>

#include "wifi_credentials.h"
#include "wifi_setup.h"

namespace sarathi::ota {

constexpr uint32_t kJoinTimeoutMs = 15000;

class WifiSetupServer {
 public:
  struct Events {
    bool ap_done = false;  // the access point started (or failed): OnAccessPointStarted(ap_ok)
    bool ap_ok = false;
    bool submitted = false;  // the phone sent a network: call WifiSetup::OnSubmitted
    bool join_done = false;  // the attempt finished: call WifiSetup::OnJoinResult(join_ok)
    bool join_ok = false;
    core::WifiCredentials credentials;  // what was submitted; save it if join_ok
  };

  // Scans for networks, then (from Poll) starts the access point (core::kSetupApName,
  // `password`), DNS and the web server. False if it couldn't even begin.
  bool Start(std::string_view password);

  // Stops everything and switches the radio off. Safe to call when not started.
  void Stop();

  // Call every loop while setup is active. `setup` is published to the page's /status.
  Events Poll(uint32_t now_ms, const core::WifiSetup& setup);
};

}  // namespace sarathi::ota
