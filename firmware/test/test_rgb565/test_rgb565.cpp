#include <unity.h>

#include "rgb565.h"

using sarathi::core::SwapRedBlue;

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

}  // namespace

int main() {
  UNITY_BEGIN();
  RUN_TEST(TestRedBecomesBlue);
  RUN_TEST(TestBlueBecomesRed);
  RUN_TEST(TestGreenUnchanged);
  RUN_TEST(TestWhiteAndBlackUnchanged);
  RUN_TEST(TestIsItsOwnInverse);
  return UNITY_END();
}
