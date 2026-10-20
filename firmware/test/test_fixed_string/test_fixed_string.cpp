#include <unity.h>

#include "fixed_string.h"

using sarathi::core::FixedString;

// Unity calls these by name, so they need external linkage and Unity's spelling.
void setUp() {}     // NOLINT(misc-use-internal-linkage,readability-identifier-naming)
void tearDown() {}  // NOLINT(misc-use-internal-linkage,readability-identifier-naming)

namespace {

void TestStartsEmpty() {
  const FixedString<8> s;
  TEST_ASSERT_TRUE(s.Empty());
  TEST_ASSERT_EQUAL_STRING("", s.CStr());
}

void TestFitsExactly() {
  const FixedString<5> s("hello");
  TEST_ASSERT_EQUAL_STRING("hello", s.CStr());
  TEST_ASSERT_EQUAL_UINT(5, s.Size());
}

void TestTruncatesLongInput() {
  const FixedString<4> s("roundabout");
  TEST_ASSERT_EQUAL_STRING("roun", s.CStr());
}

void TestNeverSplitsUtf8() {
  // "sārathi": 's' then 'ā' as two bytes (0xC4 0x81). Four bytes would cut 'ā' after "sā".
  const FixedString<2> s("s\xC4\x81rathi");
  TEST_ASSERT_EQUAL_STRING("s", s.CStr());
  const FixedString<3> t("s\xC4\x81rathi");
  TEST_ASSERT_EQUAL_STRING("s\xC4\x81", t.CStr());
}

void TestReassignAndCompare() {
  FixedString<16> a("Station Road");
  const FixedString<16> b("Station Road");
  TEST_ASSERT_TRUE(a == b);
  a.Assign("Hill Street");
  TEST_ASSERT_TRUE(a != b);
  TEST_ASSERT_EQUAL_STRING("Hill Street", a.CStr());
}

}  // namespace

int main() {
  UNITY_BEGIN();
  RUN_TEST(TestStartsEmpty);
  RUN_TEST(TestFitsExactly);
  RUN_TEST(TestTruncatesLongInput);
  RUN_TEST(TestNeverSplitsUtf8);
  RUN_TEST(TestReassignAndCompare);
  return UNITY_END();
}
