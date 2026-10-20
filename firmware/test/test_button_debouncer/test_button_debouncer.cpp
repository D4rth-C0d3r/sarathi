#include <cstdint>

#include <unity.h>

#include "button_debouncer.h"

using sarathi::core::ButtonDebouncer;
using sarathi::core::ButtonGesture;
using sarathi::core::ButtonUpdate;

// Unity calls these by name, so they need external linkage and Unity's spelling.
void setUp() {}     // NOLINT(misc-use-internal-linkage,readability-identifier-naming)
void tearDown() {}  // NOLINT(misc-use-internal-linkage,readability-identifier-naming)

namespace {

constexpr uint32_t kDebounceMs = 50;
constexpr uint32_t kDoubleClickMs = 300;
constexpr uint32_t kTickMs = 5;

// Feeds a constant raw level every tick for `duration_ms`, starting at `now`.
// Returns the last gesture seen (if any) and advances `now`.
ButtonGesture Hold(ButtonDebouncer& b, bool level, uint32_t& now, uint32_t duration_ms) {
  ButtonGesture seen = ButtonGesture::kNone;
  for (uint32_t elapsed = 0; elapsed < duration_ms; elapsed += kTickMs, now += kTickMs) {
    const ButtonUpdate u = b.Update(level, now);
    if (u.gesture != ButtonGesture::kNone) {
      seen = u.gesture;
    }
  }
  return seen;
}

void TestStartsReleased() {
  ButtonDebouncer b(kDebounceMs, kDoubleClickMs);
  TEST_ASSERT_FALSE(b.Pressed());
}

void TestShortGlitchIsIgnored() {
  ButtonDebouncer b(kDebounceMs, kDoubleClickMs);
  uint32_t now = 1000;
  Hold(b, true, now, 20);
  Hold(b, false, now, 100);
  TEST_ASSERT_FALSE(b.Pressed());
}

void TestPressRegistersAfterDebounce() {
  ButtonDebouncer b(kDebounceMs, kDoubleClickMs);
  uint32_t now = 1000;
  TEST_ASSERT_FALSE(b.Update(true, now).pressed);
  TEST_ASSERT_FALSE(b.Update(true, now + kDebounceMs - 1).pressed);
  const ButtonUpdate u = b.Update(true, now + kDebounceMs);
  TEST_ASSERT_TRUE(u.pressed);
  TEST_ASSERT_TRUE(u.changed);
}

void TestBouncyPressStillRegistersOnce() {
  ButtonDebouncer b(kDebounceMs, kDoubleClickMs);
  uint32_t now = 1000;
  int changes = 0;
  const bool bounce[] = {true, false, true, false, true};  // NOLINT(*-avoid-c-arrays)
  for (const bool level : bounce) {
    changes += b.Update(level, now).changed ? 1 : 0;
    now += 2;
  }
  for (int i = 0; i < 20; ++i, now += kTickMs) {
    changes += b.Update(true, now).changed ? 1 : 0;
  }
  TEST_ASSERT_TRUE(b.Pressed());
  TEST_ASSERT_EQUAL_INT(1, changes);
}

void TestSingleClickFiresAfterWindow() {
  ButtonDebouncer b(kDebounceMs, kDoubleClickMs);
  uint32_t now = 1000;
  TEST_ASSERT_EQUAL(ButtonGesture::kNone, Hold(b, true, now, 100));
  // Released, but the double-click window is still open: no gesture yet.
  TEST_ASSERT_EQUAL(ButtonGesture::kNone, Hold(b, false, now, kDebounceMs + 100));
  TEST_ASSERT_EQUAL(ButtonGesture::kClick, Hold(b, false, now, kDoubleClickMs));
}

void TestDoubleClick() {
  ButtonDebouncer b(kDebounceMs, kDoubleClickMs);
  uint32_t now = 1000;
  Hold(b, true, now, 80);
  Hold(b, false, now, 80);
  Hold(b, true, now, 80);
  TEST_ASSERT_EQUAL(ButtonGesture::kDoubleClick, Hold(b, false, now, 80));
  // No trailing single click afterwards.
  TEST_ASSERT_EQUAL(ButtonGesture::kNone, Hold(b, false, now, kDoubleClickMs * 2));
}

void TestTwoSlowPressesAreTwoClicks() {
  ButtonDebouncer b(kDebounceMs, kDoubleClickMs);
  uint32_t now = 1000;
  Hold(b, true, now, 80);
  TEST_ASSERT_EQUAL(ButtonGesture::kClick, Hold(b, false, now, kDoubleClickMs + 100));
  Hold(b, true, now, 80);
  TEST_ASSERT_EQUAL(ButtonGesture::kClick, Hold(b, false, now, kDoubleClickMs + 100));
}

void TestWorksAcrossMillisWrap() {
  ButtonDebouncer b(kDebounceMs, kDoubleClickMs);
  uint32_t now = UINT32_MAX - 120;
  Hold(b, true, now, 80);
  TEST_ASSERT_EQUAL(ButtonGesture::kClick, Hold(b, false, now, kDoubleClickMs + 100));
}

constexpr uint32_t kLongPressMs = 800;

void TestLongPressDisabledByDefault() {
  ButtonDebouncer b(kDebounceMs, kDoubleClickMs);
  uint32_t now = 1000;
  TEST_ASSERT_EQUAL(ButtonGesture::kNone, Hold(b, true, now, 3000));
  TEST_ASSERT_EQUAL(ButtonGesture::kClick, Hold(b, false, now, kDoubleClickMs + 100));
}

void TestLongPressFiresOnceWhileHeld() {
  ButtonDebouncer b(kDebounceMs, kDoubleClickMs, kLongPressMs);
  uint32_t now = 1000;
  int long_presses = 0;
  for (uint32_t elapsed = 0; elapsed < 3000; elapsed += kTickMs, now += kTickMs) {
    if (b.Update(true, now).gesture == ButtonGesture::kLongPress) {
      ++long_presses;
    }
  }
  TEST_ASSERT_EQUAL_INT(1, long_presses);
}

void TestLongPressReleaseIsNotAClick() {
  ButtonDebouncer b(kDebounceMs, kDoubleClickMs, kLongPressMs);
  uint32_t now = 1000;
  TEST_ASSERT_EQUAL(ButtonGesture::kLongPress, Hold(b, true, now, 1000));
  TEST_ASSERT_EQUAL(ButtonGesture::kNone, Hold(b, false, now, kDoubleClickMs * 2));
  // The next short press is a normal click again.
  Hold(b, true, now, 80);
  TEST_ASSERT_EQUAL(ButtonGesture::kClick, Hold(b, false, now, kDoubleClickMs + 100));
}

void TestShortPressStillClicksWithLongPressEnabled() {
  ButtonDebouncer b(kDebounceMs, kDoubleClickMs, kLongPressMs);
  uint32_t now = 1000;
  TEST_ASSERT_EQUAL(ButtonGesture::kNone, Hold(b, true, now, 200));
  TEST_ASSERT_EQUAL(ButtonGesture::kClick, Hold(b, false, now, kDoubleClickMs + 100));
}

}  // namespace

int main() {
  UNITY_BEGIN();
  RUN_TEST(TestStartsReleased);
  RUN_TEST(TestShortGlitchIsIgnored);
  RUN_TEST(TestPressRegistersAfterDebounce);
  RUN_TEST(TestBouncyPressStillRegistersOnce);
  RUN_TEST(TestSingleClickFiresAfterWindow);
  RUN_TEST(TestDoubleClick);
  RUN_TEST(TestTwoSlowPressesAreTwoClicks);
  RUN_TEST(TestWorksAcrossMillisWrap);
  RUN_TEST(TestLongPressDisabledByDefault);
  RUN_TEST(TestLongPressFiresOnceWhileHeld);
  RUN_TEST(TestLongPressReleaseIsNotAClick);
  RUN_TEST(TestShortPressStillClicksWithLongPressEnabled);
  return UNITY_END();
}
