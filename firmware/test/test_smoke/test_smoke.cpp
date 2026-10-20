// Proves the native toolchain and Unity runner work. Real tests for sarathi_core and
// sarathi_proto replace this as those libraries grow.

#include <unity.h>

// Unity calls these by name, so they need external linkage and Unity's spelling.
void setUp() {}     // NOLINT(misc-use-internal-linkage,readability-identifier-naming)
void tearDown() {}  // NOLINT(misc-use-internal-linkage,readability-identifier-naming)

namespace {

void TestToolchainWorks() { TEST_ASSERT_EQUAL_INT(4, 2 + 2); }

}  // namespace

int main() {
  UNITY_BEGIN();
  RUN_TEST(TestToolchainWorks);
  return UNITY_END();
}
