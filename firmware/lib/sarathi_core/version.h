// The running firmware's version (docs/ota/manifest.md). The release build sets both from the
// git tag, for example -DSARATHI_VERSION='"0.3.0"' -DSARATHI_SEQ=300; local builds are
// "0.0.0-dev" with seq 0, so any release is newer.

#pragma once

#include <cstdint>
#include <string_view>

#include "ota_version.h"

#ifndef SARATHI_VERSION
#define SARATHI_VERSION "0.0.0-dev"
#endif
#ifndef SARATHI_SEQ
#define SARATHI_SEQ 0
#endif

namespace sarathi::core {

constexpr std::string_view kFirmwareVersion = SARATHI_VERSION;
constexpr uint32_t kFirmwareSeq = SARATHI_SEQ;

static_assert(kFirmwareVersion == "0.0.0-dev" ? kFirmwareSeq == 0
                                              : SeqForVersion(kFirmwareVersion) == kFirmwareSeq,
              "SARATHI_SEQ must match SARATHI_VERSION (MAJOR*10000 + MINOR*100 + PATCH)");

}  // namespace sarathi::core
