// What the Nav screen shows besides the guidance itself, and the link status in words: which
// note wins the status spot, and when. Pure logic so the rules (CLAUDE.md, Rider safety;
// protocol.md section 5; ADR-0003) run as native tests; sarathi_ui only draws the result.

#pragma once

#include <cstdint>

#include "app_state.h"
#include "fixed_string.h"
#include "theme.h"

namespace sarathi::core {

// Route data older than this is shown as stale, never silently frozen.
constexpr uint32_t kPositionStaleMs = 3000;
// A backend switch (a fallback) is announced for this long.
constexpr uint32_t kBackendNoticeMs = 10000;

enum class NavNoteKind : uint8_t {
  kNone,
  kWarning,      // stale or missing data, rerouting, off route
  kNotice,       // the backend switched
  kAttribution,  // backend name and attribution: only while stopped
};

struct NavNote {
  NavNoteKind kind = NavNoteKind::kNone;
  FontRole role = FontRole::kSupporting;
  FixedString<96> text;
};

// The note for the status spot of the Nav map, at most one, warnings first. Nothing when not
// navigating (the idle text says why).
[[nodiscard]] NavNote NavStatus(const AppState& state, uint32_t now_ms);

// The Nav screen's text when nothing is being navigated.
[[nodiscard]] const char* NavIdleText(const AppState& state);

// The phone link in a few words, for Settings ("Phone: ...").
[[nodiscard]] const char* LinkText(LinkStatus status);

}  // namespace sarathi::core
