// The saved Wi-Fi network in NVS (ADR-0004 §2): one versioned blob (core::wifi_credentials)
// under namespace "wifi", key "cfg", written once per successful setup. NVS isn't encrypted;
// see SECURITY.md.

#pragma once

#include "wifi_credentials.h"

namespace sarathi::settings {

class NvsWifi {
 public:
  // Opens the "wifi" namespace. Returns false if NVS can't be opened.
  bool Begin();

  // False if nothing valid is saved.
  bool Load(core::WifiCredentials* out);

  bool Save(const core::WifiCredentials& credentials);

  // "Forget Wi-Fi".
  bool Erase();
};

}  // namespace sarathi::settings
