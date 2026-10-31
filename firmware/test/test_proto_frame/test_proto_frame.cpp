// Protocol framing against the golden vectors (docs/protocol/vectors/crc16.json and
// framing.json), plus edge cases the vectors don't cover.

#include <cstdint>
#include <string>
#include <vector>

#include <unity.h>

#include "../protocol_vectors.h"
#include "frame.h"

using protocol_vectors::Doc;
using protocol_vectors::Elements;
using protocol_vectors::Hex;
using protocol_vectors::Integer;
using protocol_vectors::Need;
using protocol_vectors::Text;
using sarathi::proto::Crc16;
using sarathi::proto::Drop;
using sarathi::proto::DropName;
using sarathi::proto::FeedResult;
using sarathi::proto::Fragmenter;
using sarathi::proto::kMaxMessage;
using sarathi::proto::Reassembler;

// Unity calls these by name, so they need external linkage and Unity's spelling.
void setUp() {}     // NOLINT(misc-use-internal-linkage,readability-identifier-naming)
void tearDown() {}  // NOLINT(misc-use-internal-linkage,readability-identifier-naming)

namespace {

std::vector<uint8_t> g_storage(kMaxMessage);

// One event as the vectors write it: "drop:<reason>" or "message:<type>:<msg_id>:<hex>".
std::vector<std::string> Events(const FeedResult& r) {
  std::vector<std::string> out;
  for (size_t i = 0; i < r.drop_count; ++i) {
    out.push_back(std::string("drop:") + DropName(r.drops.at(i)));
  }
  if (r.has_message) {
    std::string hex;
    char byte[3];
    for (size_t i = 0; i < r.message.size; ++i) {
      std::snprintf(byte, sizeof(byte), "%02x", r.message.payload[i]);  // NOLINT
      hex += byte;
    }
    out.push_back("message:" + std::to_string(r.message.type) + ":" +
                  std::to_string(r.message.msg_id) + ":" + hex);
  }
  return out;
}

void TestCrcVectors() {
  const Doc doc = protocol_vectors::Load("crc16.json");
  const auto cases = Elements(doc, Need(doc, 0, "cases"));
  TEST_ASSERT_TRUE(!cases.empty());
  for (const int c : cases) {
    const auto input = Hex(Text(doc, Need(doc, c, "input")));
    const long want = Integer(doc, Need(doc, c, "crc"));
    TEST_ASSERT_EQUAL_HEX16(want, Crc16(input.data(), input.size()));
  }
}

void TestSendVectors() {
  const Doc doc = protocol_vectors::Load("framing.json");
  const auto cases = Elements(doc, Need(doc, 0, "send"));
  TEST_ASSERT_TRUE(!cases.empty());
  for (const int c : cases) {
    const std::string name = Text(doc, Need(doc, c, "name"));
    const auto payload = Hex(Text(doc, Need(doc, c, "payload")));
    const auto mtu = static_cast<size_t>(Integer(doc, Need(doc, c, "mtu")));
    const auto frames = Elements(doc, Need(doc, c, "frames"));
    Fragmenter f;
    TEST_ASSERT_TRUE_MESSAGE(f.Begin(static_cast<uint8_t>(Integer(doc, Need(doc, c, "type"))),
                                     static_cast<uint8_t>(Integer(doc, Need(doc, c, "msg_id"))),
                                     payload.data(), payload.size(), mtu),
                             name.c_str());
    TEST_ASSERT_EQUAL_MESSAGE(frames.size(), f.FrameCount(), name.c_str());
    std::vector<uint8_t> out(mtu - 3);
    for (const int want_token : frames) {
      const auto want = Hex(Text(doc, want_token));
      const size_t n = f.Next(out.data(), out.size());
      TEST_ASSERT_EQUAL_MESSAGE(want.size(), n, name.c_str());
      TEST_ASSERT_EQUAL_HEX8_ARRAY_MESSAGE(want.data(), out.data(), n, name.c_str());
    }
    TEST_ASSERT_EQUAL_MESSAGE(0, f.Next(out.data(), out.size()), name.c_str());
  }
}

void TestReceiveVectors() {
  const Doc doc = protocol_vectors::Load("framing.json");
  const auto cases = Elements(doc, Need(doc, 0, "receive"));
  TEST_ASSERT_TRUE(!cases.empty());
  for (const int c : cases) {
    const std::string name = Text(doc, Need(doc, c, "name"));
    std::vector<std::string> want;
    for (const int e : Elements(doc, Need(doc, c, "events"))) {
      const int drop = protocol_vectors::Member(doc, e, "drop");
      if (drop >= 0) {
        want.push_back("drop:" + Text(doc, drop));
        continue;
      }
      const int m = Need(doc, e, "message");
      want.push_back("message:" + Text(doc, Need(doc, m, "type")) + ":" +
                     Text(doc, Need(doc, m, "msg_id")) + ":" + Text(doc, Need(doc, m, "payload")));
    }
    Reassembler r(g_storage.data(), g_storage.size());
    std::vector<std::string> got;
    for (const int f : Elements(doc, Need(doc, c, "frames"))) {
      const auto frame = Hex(Text(doc, f));
      for (auto& e : Events(r.Feed(frame.data(), frame.size()))) {
        got.push_back(std::move(e));
      }
    }
    TEST_ASSERT_EQUAL_MESSAGE(want.size(), got.size(), name.c_str());
    for (size_t i = 0; i < want.size(); ++i) {
      TEST_ASSERT_EQUAL_STRING_MESSAGE(want.at(i).c_str(), got.at(i).c_str(), name.c_str());
    }
  }
}

// Every send vector, fragmented here and fed back, comes out whole.
void TestRoundTrip() {
  std::vector<uint8_t> payload(kMaxMessage);
  for (size_t i = 0; i < payload.size(); ++i) {
    payload.at(i) = static_cast<uint8_t>(i * 7U + 3U);
  }
  for (const size_t mtu : {size_t{247}, size_t{517}}) {
    for (const size_t size : {size_t{0}, size_t{1}, size_t{237}, size_t{238}, size_t{5000}}) {
      Fragmenter f;
      TEST_ASSERT_TRUE(f.Begin(0x43, 9, payload.data(), size, mtu));
      Reassembler r(g_storage.data(), g_storage.size());
      std::vector<uint8_t> frame(mtu - 3);
      bool done = false;
      size_t n = 0;
      while ((n = f.Next(frame.data(), frame.size())) > 0) {
        TEST_ASSERT_FALSE(done);
        const FeedResult res = r.Feed(frame.data(), n);
        TEST_ASSERT_EQUAL(0, res.drop_count);
        if (res.has_message) {
          done = true;
          TEST_ASSERT_EQUAL(size, res.message.size);
          if (size > 0) {
            TEST_ASSERT_EQUAL_HEX8_ARRAY(payload.data(), res.message.payload, size);
          }
        }
      }
      TEST_ASSERT_TRUE(done);
    }
  }
}

void TestFragmenterLimits() {
  std::vector<uint8_t> payload(kMaxMessage + 1);
  Fragmenter f;
  TEST_ASSERT_FALSE(f.Begin(1, 1, payload.data(), kMaxMessage + 1, 517));
  // 15,360 B at MTU 247 needs 64 frames: 237 + 63 × 241 = 15,420 covers it.
  TEST_ASSERT_TRUE(f.Begin(1, 1, payload.data(), kMaxMessage, 247));
  TEST_ASSERT_EQUAL(64, f.FrameCount());
  // At MTU 200 it would need more than 64 frames.
  TEST_ASSERT_FALSE(f.Begin(1, 1, payload.data(), kMaxMessage, 200));
  TEST_ASSERT_FALSE(f.Begin(1, 1, payload.data(), 1, 10));
  // A buffer smaller than MTU - 3 gets nothing.
  TEST_ASSERT_TRUE(f.Begin(1, 1, payload.data(), 10, 247));
  uint8_t small[16];
  TEST_ASSERT_EQUAL(0, f.Next(small, sizeof(small)));
}

// A buffer smaller than the declared total drops the message instead of overflowing.
void TestSmallStorage() {
  std::vector<uint8_t> payload(1000, 0xAB);
  std::vector<uint8_t> storage(500);
  Fragmenter f;
  TEST_ASSERT_TRUE(f.Begin(0x41, 2, payload.data(), payload.size(), 247));
  Reassembler r(storage.data(), storage.size());
  std::vector<uint8_t> frame(244);
  const size_t n = f.Next(frame.data(), frame.size());
  const FeedResult res = r.Feed(frame.data(), n);
  TEST_ASSERT_EQUAL(1, res.drop_count);
  TEST_ASSERT_EQUAL(Drop::kTooLong, res.drops.at(0));
  TEST_ASSERT_EQUAL(1, r.Dropped());
}

}  // namespace

int main() {
  UNITY_BEGIN();
  RUN_TEST(TestCrcVectors);
  RUN_TEST(TestSendVectors);
  RUN_TEST(TestReceiveVectors);
  RUN_TEST(TestRoundTrip);
  RUN_TEST(TestFragmenterLimits);
  RUN_TEST(TestSmallStorage);
  return UNITY_END();
}
