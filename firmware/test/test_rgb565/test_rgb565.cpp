#include <unity.h>

#include "rgb565.h"

using sarathi::core::SwapRedBlue;
using sarathi::core::SwapRedBlueRgb;
using sarathi::core::ToRgb565;

// Unity calls these by name, so they need external linkage and Unity's spelling.
void setUp() {}     // NOLINT(misc-use-internal-linkage,readability-identifier-naming)
void tearDown() {}  // NOLINT(misc-use-internal-linkage,readability-identifier-naming)

namespace {

void TestRedBecomesBlue() { TEST_ASSERT_EQUAL_HEX16(0x001F, SwapRedBlue(0xF800)); }

void TestBlueBecomesRed() { TEST_ASSERT_EQUAL_HEX16(0xF800, SwapRedBlue(0x001F)); }

void TestGreenUnchanged() { TEST_ASSERT_EQUAL_HEX16(0x07E0, SwapRedBlue(0x07E0)); }

void TestWhiteAndBlackUnchanged() {
  TEST_ASSERT_EQUAL_HEX16(0xFFFF, SwapRedBlue(0xFFFF));
  TEST_ASSERT_EQUAL_HEX16(0x0000, SwapRedBlue(0x0000));
}

void TestIsItsOwnInverse() {
  for (uint32_t c = 0; c <= 0xFFFF; c += 7) {
    const auto v = static_cast<uint16_t>(c);
    TEST_ASSERT_EQUAL_HEX16(v, SwapRedBlue(SwapRedBlue(v)));
  }
}

void TestToRgb565() {
  TEST_ASSERT_EQUAL_HEX16(0xF800, ToRgb565(0xFF0000));
  TEST_ASSERT_EQUAL_HEX16(0x07E0, ToRgb565(0x00FF00));
  TEST_ASSERT_EQUAL_HEX16(0x001F, ToRgb565(0x0000FF));
  TEST_ASSERT_EQUAL_HEX16(0x8410, ToRgb565(0x808080));
}

// Swapping before conversion gives what the flush used to produce by swapping after it.
void TestPreSwapMatchesFlushSwap() {
  for (uint32_t rgb = 0; rgb <= 0xFFFFFF; rgb += 0x010305) {
    TEST_ASSERT_EQUAL_HEX16(SwapRedBlue(ToRgb565(rgb)), ToRgb565(SwapRedBlueRgb(rgb)));
  }
}

}  // namespace

int main() {
  UNITY_BEGIN();
  RUN_TEST(TestRedBecomesBlue);
  RUN_TEST(TestBlueBecomesRed);
  RUN_TEST(TestGreenUnchanged);
  RUN_TEST(TestWhiteAndBlackUnchanged);
  RUN_TEST(TestIsItsOwnInverse);
  RUN_TEST(TestToRgb565);
  RUN_TEST(TestPreSwapMatchesFlushSwap);
  return UNITY_END();
}
