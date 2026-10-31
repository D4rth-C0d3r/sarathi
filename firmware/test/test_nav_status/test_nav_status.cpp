// The Nav screen's status note and idle text (core/nav_status): warnings first, the backend
// switch notice for 10 s, attribution only while stopped and only at the stopped-only size.

#include <cstdint>

#include <unity.h>

#include "app_state.h"
#include "legibility.h"
#include "nav_status.h"

using sarathi::core::AppState;
using sarathi::core::FontRole;
using sarathi::core::kBackendNoticeMs;
using sarathi::core::kPositionStaleMs;
using sarathi::core::LinkStatus;
using sarathi::core::NavIdleText;
using sarathi::core::NavNoteKind;
using sarathi::core::NavStatus;

// Unity calls these by name, so they need external linkage and Unity's spelling.
void setUp() {}     // NOLINT(misc-use-internal-linkage,readability-identifier-naming)
void tearDown() {}  // NOLINT(misc-use-internal-linkage,readability-identifier-naming)

namespace {

constexpr uint32_t kNow = 100000;

// Navigating with a fresh route window and a backend, stopped.
AppState Navigating() {
  AppState s;
  s.nav.active = true;
  s.nav.has_window = true;
  s.nav.window_received_ms = kNow;
  s.backend.valid = true;
  s.backend.name.Assign("OSM (offline)");
  s.backend.attribution.Assign("(c) OpenStreetMap contributors");
  return s;
}

int Kind(const AppState& s, uint32_t now = kNow) {
  return static_cast<int>(NavStatus(s, now).kind);
}

void TestStoppedShowsAttribution() {
  const AppState s = Navigating();
  const auto note = NavStatus(s, kNow);
  TEST_ASSERT_EQUAL(static_cast<int>(NavNoteKind::kAttribution), static_cast<int>(note.kind));
  TEST_ASSERT_EQUAL(static_cast<int>(FontRole::kStoppedOnly), static_cast<int>(note.role));
  TEST_ASSERT_EQUAL_STRING("OSM (offline)\n(c) OpenStreetMap contributors", note.text.CStr());
}

void TestNothingWhileRiding() {
  AppState s = Navigating();
  s.ride.moving = true;
  TEST_ASSERT_EQUAL(static_cast<int>(NavNoteKind::kNone), Kind(s));
}

void TestSwitchNoticeForTenSeconds() {
  AppState s = Navigating();
  s.ride.moving = true;
  s.backend.switched = true;
  s.backend.switched_ms = kNow;
  s.nav.window_received_ms = kNow + kBackendNoticeMs - 1;  // fresh: a stale window would win
  const auto note = NavStatus(s, kNow + kBackendNoticeMs - 1);
  TEST_ASSERT_EQUAL(static_cast<int>(NavNoteKind::kNotice), static_cast<int>(note.kind));
  TEST_ASSERT_EQUAL_STRING("Now: OSM (offline)", note.text.CStr());
  s.nav.window_received_ms = kNow + kBackendNoticeMs;
  TEST_ASSERT_EQUAL(static_cast<int>(NavNoteKind::kNone), Kind(s, kNow + kBackendNoticeMs));
}

void TestWarningsComeFirst() {
  AppState s = Navigating();
  s.backend.switched = true;
  s.backend.switched_ms = kNow;
  s.nav.off_route = true;
  TEST_ASSERT_EQUAL_STRING("Off route", NavStatus(s, kNow).text.CStr());
  s.nav.rerouting = true;
  TEST_ASSERT_EQUAL_STRING("Rerouting", NavStatus(s, kNow).text.CStr());
  s.nav.guidance_stale = true;
  TEST_ASSERT_EQUAL_STRING("Navigation stale", NavStatus(s, kNow).text.CStr());
  AppState t = Navigating();
  TEST_ASSERT_EQUAL_STRING("Position stale 4s", NavStatus(t, kNow + 4000).text.CStr());
  TEST_ASSERT_EQUAL(static_cast<int>(NavNoteKind::kAttribution), Kind(t, kNow + kPositionStaleMs));
  t.nav.has_window = false;
  TEST_ASSERT_EQUAL_STRING("No route data", NavStatus(t, kNow).text.CStr());
  // Arrow mode (no geometry, Chronos): no route warnings.
  t.caps.route_geometry = false;
  TEST_ASSERT_EQUAL(static_cast<int>(NavNoteKind::kAttribution), Kind(t));
}

// Legibility (ADR-0003): ride-time notes are at least supporting size; the stopped-only size
// never shows while moving, whatever the state.
void TestNotesKeepTheLegibilityRules() {
  for (int mask = 0; mask < 64; ++mask) {
    AppState s = Navigating();
    s.ride.moving = (mask & 1) != 0;
    s.nav.guidance_stale = (mask & 2) != 0;
    s.nav.rerouting = (mask & 4) != 0;
    s.nav.has_window = (mask & 8) != 0;
    s.backend.switched = (mask & 16) != 0;
    s.backend.switched_ms = kNow;
    s.backend.valid = (mask & 32) != 0;
    const auto note = NavStatus(s, kNow);
    if (note.kind == NavNoteKind::kNone) {
      continue;
    }
    const auto min_px = sarathi::core::kMinFontPx.at(static_cast<size_t>(note.role));
    if (s.ride.moving) {
      TEST_ASSERT_TRUE(note.role != FontRole::kStoppedOnly);
      TEST_ASSERT_TRUE(min_px >=
                       sarathi::core::kMinFontPx.at(static_cast<size_t>(FontRole::kSupporting)));
    }
    if (note.kind == NavNoteKind::kWarning) {
      TEST_ASSERT_EQUAL(static_cast<int>(FontRole::kEssential), static_cast<int>(note.role));
    }
  }
}

void TestIdleTextFollowsTheLink() {
  AppState s;
  TEST_ASSERT_EQUAL_STRING("Phone not connected", NavIdleText(s));
  s.phone.link = LinkStatus::kSecuring;
  TEST_ASSERT_EQUAL_STRING("Securing link", NavIdleText(s));
  s.phone.link = LinkStatus::kUpdateApp;
  TEST_ASSERT_EQUAL_STRING("Update the app", NavIdleText(s));
  s.phone.link = LinkStatus::kOff;
  TEST_ASSERT_EQUAL_STRING("Bluetooth paused", NavIdleText(s));
  s.phone.connected = true;
  TEST_ASSERT_EQUAL_STRING("No navigation", NavIdleText(s));
  TEST_ASSERT_EQUAL(static_cast<int>(NavNoteKind::kNone), Kind(s));  // not navigating
}

}  // namespace

int main() {
  UNITY_BEGIN();
  RUN_TEST(TestStoppedShowsAttribution);
  RUN_TEST(TestNothingWhileRiding);
  RUN_TEST(TestSwitchNoticeForTenSeconds);
  RUN_TEST(TestWarningsComeFirst);
  RUN_TEST(TestNotesKeepTheLegibilityRules);
  RUN_TEST(TestIdleTextFollowsTheLink);
  return UNITY_END();
}
