#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include <unity.h>

#include "captive_dns.h"
#include "wifi_form.h"
#include "wifi_setup.h"

using sarathi::core::BuildCaptiveDnsReply;
using sarathi::core::JsonEscape;
using sarathi::core::kJoinedLingerMs;
using sarathi::core::kSetupPasswordAlphabet;
using sarathi::core::kSetupPasswordLength;
using sarathi::core::kWifiSetupTimeoutMs;
using sarathi::core::MakeSetupPassword;
using sarathi::core::ParseWifiForm;
using sarathi::core::UrlDecode;
using sarathi::core::WifiCredentials;
using sarathi::core::WifiFormResult;
using sarathi::core::WifiSetup;
using sarathi::core::WifiSetupFailure;
using sarathi::core::WifiSetupJob;
using sarathi::core::WifiSetupPhase;

// Unity calls these by name, so they need external linkage and Unity's spelling.
void setUp() {}     // NOLINT(misc-use-internal-linkage,readability-identifier-naming)
void tearDown() {}  // NOLINT(misc-use-internal-linkage,readability-identifier-naming)

namespace {

constexpr const char* kPassword = "abcd2345";

void TestHappyPath() {
  WifiSetup s;
  TEST_ASSERT_FALSE(s.Active());
  TEST_ASSERT_TRUE(s.Start(1000, kPassword));
  TEST_ASSERT_EQUAL(WifiSetupPhase::kStarting, s.Phase());
  TEST_ASSERT_EQUAL(WifiSetupJob::kStartAccessPoint, s.TakeJob());
  TEST_ASSERT_EQUAL(WifiSetupJob::kNone, s.TakeJob());
  TEST_ASSERT_EQUAL_STRING(kPassword, std::string(s.Password()).c_str());

  s.OnAccessPointStarted(true);
  TEST_ASSERT_EQUAL(WifiSetupPhase::kWaitingForPhone, s.Phase());
  s.OnSubmitted("Home 5G");
  TEST_ASSERT_EQUAL(WifiSetupPhase::kJoining, s.Phase());
  TEST_ASSERT_EQUAL_STRING("Home 5G", std::string(s.JoiningSsid()).c_str());
  s.Tick(5000);
  s.OnJoinResult(true);
  TEST_ASSERT_EQUAL(WifiSetupPhase::kJoined, s.Phase());
  TEST_ASSERT_TRUE(s.HasSaved());
  TEST_ASSERT_EQUAL_STRING("Home 5G", std::string(s.SavedSsid()).c_str());

  // The AP stays up briefly so the phone's page can show success.
  TEST_ASSERT_TRUE(s.Active());
  s.Tick(5000 + kJoinedLingerMs - 1);
  TEST_ASSERT_EQUAL(WifiSetupJob::kNone, s.TakeJob());
  s.Tick(5000 + kJoinedLingerMs);
  TEST_ASSERT_EQUAL(WifiSetupJob::kStopAccessPoint, s.TakeJob());
  TEST_ASSERT_FALSE(s.Active());
  TEST_ASSERT_TRUE(s.Close());
  TEST_ASSERT_EQUAL(WifiSetupPhase::kIdle, s.Phase());
  TEST_ASSERT_EQUAL(WifiSetupJob::kNone, s.TakeJob());  // AP already stopped
}

void TestWrongPasswordLetsYouRetry() {
  WifiSetup s;
  s.Start(0, kPassword);
  s.OnAccessPointStarted(true);
  s.OnSubmitted("Home");
  s.OnJoinResult(false);
  TEST_ASSERT_EQUAL(WifiSetupPhase::kWaitingForPhone, s.Phase());
  TEST_ASSERT_TRUE(s.LastJoinFailed());
  TEST_ASSERT_FALSE(s.HasSaved());
  s.OnSubmitted("Home");
  TEST_ASSERT_FALSE(s.LastJoinFailed());
}

void TestTimeoutAndCancel() {
  WifiSetup s;
  s.Start(100, kPassword);
  TEST_ASSERT_EQUAL(WifiSetupJob::kStartAccessPoint, s.TakeJob());
  s.OnAccessPointStarted(true);
  s.Tick(100 + kWifiSetupTimeoutMs - 1);
  TEST_ASSERT_EQUAL(WifiSetupPhase::kWaitingForPhone, s.Phase());
  s.Tick(100 + kWifiSetupTimeoutMs);
  TEST_ASSERT_EQUAL(WifiSetupPhase::kFailed, s.Phase());
  TEST_ASSERT_EQUAL(WifiSetupFailure::kTimedOut, s.Failure());
  TEST_ASSERT_EQUAL(WifiSetupJob::kStopAccessPoint, s.TakeJob());
  TEST_ASSERT_TRUE(s.Close());

  TEST_ASSERT_TRUE(s.Start(0, kPassword));
  TEST_ASSERT_EQUAL(WifiSetupJob::kStartAccessPoint, s.TakeJob());
  TEST_ASSERT_TRUE(s.Cancel());
  TEST_ASSERT_EQUAL(WifiSetupPhase::kIdle, s.Phase());
  TEST_ASSERT_EQUAL(WifiSetupJob::kStopAccessPoint, s.TakeJob());
  s.OnAccessPointStarted(true);  // late result after cancel: ignored
  TEST_ASSERT_EQUAL(WifiSetupPhase::kIdle, s.Phase());
  TEST_ASSERT_FALSE(s.Cancel());
}

// The loop reads the time before the knob starts setup, so Tick can see a slightly older time.
void TestTickWithAnOlderTime() {
  WifiSetup s;
  s.Start(5000, kPassword);
  s.OnAccessPointStarted(true);
  s.Tick(4990);
  TEST_ASSERT_EQUAL(WifiSetupPhase::kWaitingForPhone, s.Phase());
}

void TestAccessPointFailure() {
  WifiSetup s;
  s.Start(0, kPassword);
  s.OnAccessPointStarted(false);
  TEST_ASSERT_EQUAL(WifiSetupPhase::kFailed, s.Phase());
  TEST_ASSERT_EQUAL(WifiSetupFailure::kAccessPoint, s.Failure());
}

void TestStartRules() {
  WifiSetup s;
  TEST_ASSERT_FALSE(s.Start(0, "short"));  // password must be kSetupPasswordLength
  TEST_ASSERT_TRUE(s.Start(0, kPassword));
  TEST_ASSERT_FALSE(s.Start(0, kPassword));  // already running
  TEST_ASSERT_FALSE(s.Forget());             // not while active
}

void TestForget() {
  WifiSetup s;
  TEST_ASSERT_FALSE(s.Forget());  // nothing saved
  s.SetSaved("Home");
  TEST_ASSERT_TRUE(s.Forget());
  TEST_ASSERT_EQUAL(WifiSetupJob::kForget, s.TakeJob());
  s.SetSaved("");
  TEST_ASSERT_FALSE(s.HasSaved());
}

std::string Decode(const char* text, bool* ok) {
  char out[32];
  size_t n = 0;
  *ok = UrlDecode(text, out, sizeof(out), &n);
  return *ok ? std::string(out, n) : std::string();
}

void TestUrlDecode() {
  bool ok = false;
  TEST_ASSERT_EQUAL_STRING("Home 5G", Decode("Home+5G", &ok).c_str());
  TEST_ASSERT_EQUAL_STRING("a&b=c%", Decode("a%26b%3Dc%25", &ok).c_str());
  TEST_ASSERT_EQUAL_STRING("\xC3\xA9", Decode("%C3%a9", &ok).c_str());
  Decode("bad%2", &ok);
  TEST_ASSERT_FALSE(ok);
  Decode("bad%zz", &ok);
  TEST_ASSERT_FALSE(ok);
  Decode("%", &ok);
  TEST_ASSERT_FALSE(ok);
  Decode("0123456789012345678901234567890123", &ok);  // longer than the buffer
  TEST_ASSERT_FALSE(ok);
}

void TestParseWifiForm() {
  WifiCredentials c;
  TEST_ASSERT_EQUAL(WifiFormResult::kOk, ParseWifiForm("ssid=Home+5G&password=secret%2112", &c));
  TEST_ASSERT_EQUAL_STRING("Home 5G", c.ssid.CStr());
  TEST_ASSERT_EQUAL_STRING("secret!12", c.password.CStr());
  TEST_ASSERT_EQUAL(WifiFormResult::kOk, ParseWifiForm("password=&x=1&ssid=cafe", &c));  // open
  TEST_ASSERT_EQUAL_STRING("", c.password.CStr());

  TEST_ASSERT_EQUAL(WifiFormResult::kMalformed, ParseWifiForm("ssid=Home", &c));
  TEST_ASSERT_EQUAL(WifiFormResult::kMalformed, ParseWifiForm("ssid=a&ssid=b&password=", &c));
  TEST_ASSERT_EQUAL(WifiFormResult::kBadSsid, ParseWifiForm("ssid=&password=12345678", &c));
  TEST_ASSERT_EQUAL(WifiFormResult::kBadSsid,
                    ParseWifiForm(("ssid=" + std::string(33, 'x') + "&password=").c_str(), &c));
  TEST_ASSERT_EQUAL(WifiFormResult::kBadPassword, ParseWifiForm("ssid=Home&password=short", &c));
  TEST_ASSERT_EQUAL(WifiFormResult::kBadPassword, ParseWifiForm("ssid=Home&password=%zz", &c));
}

void TestJsonEscape() {
  char out[32];
  size_t n = 0;
  TEST_ASSERT_TRUE(JsonEscape("a\"b\\c\n\x01\xC3\xA9", out, sizeof(out), &n));
  TEST_ASSERT_EQUAL_STRING("a\\\"b\\\\c\\u000a\\u0001\xC3\xA9", out);
  TEST_ASSERT_EQUAL(n, std::string(out).size());
  char tiny[4];
  TEST_ASSERT_FALSE(JsonEscape("\"\"", tiny, sizeof(tiny), &n));  // 4 chars + NUL won't fit
}

uint32_t counter = 0;  // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)
uint32_t Counting() { return counter++; }
uint32_t AlwaysHigh() { return UINT32_MAX; }
uint32_t high_then_low_calls = 0;  // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)
uint32_t HighThenLow() { return high_then_low_calls++ % 2 == 0 ? UINT32_MAX : 5; }

void TestSetupPassword() {
  char out[kSetupPasswordLength + 1];
  MakeSetupPassword(Counting, out);
  TEST_ASSERT_EQUAL(kSetupPasswordLength, std::string(out).size());
  TEST_ASSERT_EQUAL_STRING("abcdefgh", out);  // 0..7 map to the first letters
  for (const char c : std::string(out)) {
    TEST_ASSERT_TRUE(kSetupPasswordAlphabet.find(c) != std::string_view::npos);
  }
  // Values in the biased top range are redrawn, never used.
  MakeSetupPassword(HighThenLow, out);
  TEST_ASSERT_EQUAL_STRING("ffffffff", out);
  for (const char c : std::string(kSetupPasswordAlphabet)) {
    TEST_ASSERT_TRUE(std::string("0O1lI").find(c) == std::string::npos);
  }
  static_cast<void>(AlwaysHigh);
}

// A query for "a.bc" (A, IN) with ID 0x1234 and recursion desired.
std::vector<uint8_t> DnsQuery(uint16_t qtype, uint16_t flags = 0x0100) {
  return {0x12,
          0x34,
          static_cast<uint8_t>(flags >> 8),
          static_cast<uint8_t>(flags),
          0,
          1,
          0,
          0,
          0,
          0,
          0,
          0,
          1,
          'a',
          2,
          'b',
          'c',
          0,
          0,
          static_cast<uint8_t>(qtype),
          0,
          1};
}

void TestCaptiveDns() {
  const std::array<uint8_t, 4> ip = {192, 168, 4, 1};
  uint8_t out[512];
  const std::vector<uint8_t> q = DnsQuery(1);
  const size_t n = BuildCaptiveDnsReply(q.data(), q.size(), ip, out, sizeof(out));
  TEST_ASSERT_EQUAL(q.size() + 16, n);
  TEST_ASSERT_EQUAL_HEX8(0x12, out[0]);  // same ID
  TEST_ASSERT_EQUAL_HEX8(0x34, out[1]);
  TEST_ASSERT_EQUAL_HEX8(0x85, out[2]);                       // QR, AA, RD
  TEST_ASSERT_EQUAL_HEX8(0x80, out[3]);                       // RA, no error
  TEST_ASSERT_EQUAL(1, out[7]);                               // one answer
  TEST_ASSERT_EQUAL_MEMORY(&q[12], &out[12], q.size() - 12);  // question echoed
  const uint8_t answer[] = {0xC0, 0x0C, 0, 1, 0, 1, 0, 0, 0, 60, 0, 4, 192, 168, 4, 1};
  TEST_ASSERT_EQUAL_MEMORY(answer, &out[q.size()], sizeof(answer));

  const std::vector<uint8_t> aaaa = DnsQuery(28);  // IPv6: empty answer
  TEST_ASSERT_EQUAL(aaaa.size(), BuildCaptiveDnsReply(aaaa.data(), aaaa.size(), ip, out, 512));
  TEST_ASSERT_EQUAL(0, out[7]);

  const std::vector<uint8_t> reply = DnsQuery(1, 0x8100);  // a response, not a query
  TEST_ASSERT_EQUAL(0, BuildCaptiveDnsReply(reply.data(), reply.size(), ip, out, 512));
  std::vector<uint8_t> truncated = q;
  truncated.resize(15);
  TEST_ASSERT_EQUAL(0, BuildCaptiveDnsReply(truncated.data(), truncated.size(), ip, out, 512));
  std::vector<uint8_t> bad_label = q;
  bad_label[12] = 0xC0;  // compression pointer in a question
  TEST_ASSERT_EQUAL(0, BuildCaptiveDnsReply(bad_label.data(), bad_label.size(), ip, out, 512));
  TEST_ASSERT_EQUAL(0, BuildCaptiveDnsReply(q.data(), q.size(), ip, out, q.size()));  // no room
}

}  // namespace

int main() {
  UNITY_BEGIN();
  RUN_TEST(TestHappyPath);
  RUN_TEST(TestWrongPasswordLetsYouRetry);
  RUN_TEST(TestTimeoutAndCancel);
  RUN_TEST(TestTickWithAnOlderTime);
  RUN_TEST(TestAccessPointFailure);
  RUN_TEST(TestStartRules);
  RUN_TEST(TestForget);
  RUN_TEST(TestUrlDecode);
  RUN_TEST(TestParseWifiForm);
  RUN_TEST(TestJsonEscape);
  RUN_TEST(TestSetupPassword);
  RUN_TEST(TestCaptiveDns);
  return UNITY_END();
}
