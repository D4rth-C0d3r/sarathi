// JSON messages against the golden vectors (docs/protocol/vectors/json_messages.json):
// phone → device messages decode and re-encode to the vector's canonical bytes, device →
// phone messages built here encode to them, and invalid messages fail for the vector's reason.
// Also the JsonReader token accessors the codec relies on.

#include <cstdint>
#include <map>
#include <string>
#include <variant>
#include <vector>

#include <unity.h>

#include "../protocol_vectors.h"
#include "json_codec.h"
#include "json_reader.h"
#include "json_writer.h"
#include "messages.h"

using protocol_vectors::Doc;
using protocol_vectors::Elements;
using protocol_vectors::Integer;
using protocol_vectors::Need;
using sarathi::core::JsonGet;
using sarathi::core::JsonReader;
using sarathi::core::kNoToken;
using sarathi::core::kRootToken;
using namespace sarathi::proto;  // NOLINT(google-build-using-namespace)

// Unity calls these by name, so they need external linkage and Unity's spelling.
void setUp() {}     // NOLINT(misc-use-internal-linkage,readability-identifier-naming)
void tearDown() {}  // NOLINT(misc-use-internal-linkage,readability-identifier-naming)

namespace {

PhoneJsonDecoder g_decoder;

// The vectors keep JSON text as JSON strings; jsmn hands back the escaped form.
std::string Unescape(const std::string& raw) {
  const std::string doc = "{\"s\":\"" + raw + "\"}";
  JsonReader r;
  TEST_ASSERT_TRUE(r.Parse(doc));
  std::string out(raw.size() + 1, '\0');
  size_t n = 0;
  TEST_ASSERT_EQUAL(
      static_cast<int>(JsonGet::kOk),
      static_cast<int>(r.Utf8At(r.Member(kRootToken, "s"), out.data(), out.size(), &n)));
  out.resize(n);
  return out;
}

std::string Field(const Doc& doc, int object, const char* name) {
  return Unescape(protocol_vectors::Text(doc, Need(doc, object, name)));
}

template <typename M>
std::string Encode(const M& m) {
  std::string out(kMaxJson, '\0');
  out.resize(EncodeJson(m, out.data(), out.size()));
  return out;
}

JsonError Decode(uint8_t type, const std::string& payload, PhoneMessage* out) {
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
  return g_decoder.Decode(type, reinterpret_cast<const uint8_t*>(payload.data()), payload.size(),
                          out);
}

// The device → phone vectors, built from structs.
std::map<std::string, std::string> DeviceMessages() {
  std::map<std::string, std::string> out;
  HelloDevice hello;
  hello.proto_minor = 0;  // the vectors are a 1.0 dash
  hello.fw.Assign("0.5.0");
  hello.seq = 500;
  hello.styles.at(0).Assign("classic");
  hello.styles.at(1).Assign("hud");
  hello.styles.at(2).Assign("racing");
  hello.styles.at(3).Assign("minimal");
  hello.style_count = 4;
  hello.chunk_kb = 1536;
  hello.max_msg = 15360;
  hello.has_route = true;
  hello.route_id = 3;
  hello.route_v = 2;
  hello.has_epoch = true;
  hello.epoch = 7;
  out["hello-device"] = Encode(hello);
  hello.style_count = 1;
  hello.has_route = false;
  hello.has_epoch = false;
  out["hello-device-empty"] = Encode(hello);

  const ChunkKey stored{0, 11553, 7334, 20261018};
  const RefusedChunk refused{{1, 2888, 1833, 20261018}, RefuseReason::kFull};
  out["chunk-ack"] = Encode(ChunkAck{&stored, 1, &refused, 1});
  out["flow"] = Encode(Flow{32, 1});
  out["style"] = Encode(Style{MapStyle::kHud, Bit(MapLayer::kWater)});
  out["call-action"] = Encode(CallAction{CallChoice::kReject});
  out["media-cmd"] = Encode(MediaCmd{MediaKey::kNext});
  return out;
}

void TestValidVectors() {
  const Doc doc = protocol_vectors::Load("json_messages.json");
  const auto device = DeviceMessages();
  size_t phone_count = 0;
  size_t device_count = 0;
  for (const int c : Elements(doc, Need(doc, 0, "valid"))) {
    const std::string name = Field(doc, c, "name");
    const std::string canonical = Field(doc, c, "canonical");
    if (Field(doc, c, "direction") == "device") {
      const auto it = device.find(name);
      TEST_ASSERT_TRUE_MESSAGE(it != device.end(), name.c_str());
      TEST_ASSERT_EQUAL_STRING_MESSAGE(canonical.c_str(), it->second.c_str(), name.c_str());
      ++device_count;
      continue;
    }
    PhoneMessage m;
    const auto type = static_cast<uint8_t>(Integer(doc, Need(doc, c, "type")));
    const JsonError e = Decode(type, Field(doc, c, "payload"), &m);
    TEST_ASSERT_EQUAL_STRING_MESSAGE("none", JsonErrorName(e), name.c_str());
    const std::string again = std::visit([](const auto& v) { return Encode(v); }, m);
    TEST_ASSERT_EQUAL_STRING_MESSAGE(canonical.c_str(), again.c_str(), name.c_str());
    ++phone_count;
  }
  TEST_ASSERT_EQUAL(device.size(), device_count);
  TEST_ASSERT_TRUE(phone_count >= 15);
}

void TestInvalidVectors() {
  const Doc doc = protocol_vectors::Load("json_messages.json");
  size_t checked = 0;
  for (const int c : Elements(doc, Need(doc, 0, "invalid"))) {
    if (Field(doc, c, "direction") != "phone") {
      continue;  // the device never decodes its own messages
    }
    const std::string name = Field(doc, c, "name");
    PhoneMessage m;
    const auto type = static_cast<uint8_t>(Integer(doc, Need(doc, c, "type")));
    const JsonError e = Decode(type, Field(doc, c, "payload"), &m);
    TEST_ASSERT_EQUAL_STRING_MESSAGE(Field(doc, c, "error").c_str(), JsonErrorName(e),
                                     name.c_str());
    ++checked;
  }
  TEST_ASSERT_TRUE(checked >= 10);
}

void TestDecodedValues() {
  PhoneMessage m;
  TEST_ASSERT_EQUAL(
      static_cast<int>(JsonError::kNone),
      static_cast<int>(Decode(0x07,
                              R"({"t":"nav","m":"teleport","exit":3,"dist_m":1000000,)"
                              R"("street":"Gä\"x","rem_s":604800,"rem_m":10000000,)"
                              R"("flags":["arrived","rerouting","arrived"]})",
                              &m)));
  const Nav& nav = std::get<Nav>(m);
  TEST_ASSERT_EQUAL(static_cast<int>(Maneuver::kStraight),
                    static_cast<int>(ManeuverFromName(nav.m.View())));
  TEST_ASSERT_EQUAL(3, nav.exit);
  TEST_ASSERT_EQUAL(1000000U, nav.dist_m);
  TEST_ASSERT_EQUAL_STRING("G\xC3\xA4\"x", nav.street.CStr());
  TEST_ASSERT_EQUAL(Bit(NavFlag::kRerouting) | Bit(NavFlag::kArrived), nav.flags);

  TEST_ASSERT_EQUAL(
      static_cast<int>(JsonError::kNone),
      static_cast<int>(
          Decode(0x02, R"({"t":"time","local":"2026-01-02T03:04:05","utc_offset_min":-720})", &m)));
  TEST_ASSERT_EQUAL(-720, std::get<Time>(m).utc_offset_min);
  TEST_ASSERT_EQUAL_STRING(R"({"t":"time","local":"2026-01-02T03:04:05","utc_offset_min":-720})",
                           Encode(std::get<Time>(m)).c_str());
}

void TestDecodeRejects() {
  PhoneMessage m;
  const auto error = [&m](uint8_t type, const char* json) {
    return std::string(JsonErrorName(Decode(type, json, &m)));
  };
  // Device → phone types and unknown types are not the device's to decode.
  TEST_ASSERT_EQUAL_STRING("unknown_type", error(0x11, R"({"t":"flow"})").c_str());
  TEST_ASSERT_EQUAL_STRING("unknown_type", error(0x0C, "{}").c_str());
  // An optional event field is still checked when present.
  TEST_ASSERT_EQUAL_STRING("invalid",
                           error(0x06, R"({"t":"event","kind":"reroute","percent":101})").c_str());
  TEST_ASSERT_EQUAL_STRING(
      "invalid", error(0x06, R"({"t":"event","kind":"cost_warning","backend":"google"})").c_str());
  // hello's proto is exactly two numbers.
  TEST_ASSERT_EQUAL_STRING(
      "invalid", error(0x01, R"({"t":"hello","proto":[1,0,0],"app":"x","caps":[]})").c_str());
  TEST_ASSERT_EQUAL_STRING(
      "invalid", error(0x01, R"({"t":"hello","proto":[1,0],"app":"x","caps":["wifi"]})").c_str());
  TEST_ASSERT_EQUAL_STRING(
      "invalid", error(0x03, R"({"t":"phone_batt","level":7.0,"charging":true})").c_str());
  TEST_ASSERT_EQUAL_STRING(
      "invalid", error(0x09, R"({"t":"call","phase":"idle","caller":"a\u0000b"})").c_str());
  TEST_ASSERT_EQUAL_STRING(
      "invalid",
      error(0x02, R"({"t":"time","local":"2026-01-02T03:04:05","utc_offset_min":841})").c_str());
  // A repeated name is refused rather than guessed.
  TEST_ASSERT_EQUAL_STRING("invalid", error(0x04, R"({"t":"frame","epoch":1,"epoch":2})").c_str());
  TEST_ASSERT_EQUAL_STRING("type_mismatch", error(0x04, R"({"epoch":1})").c_str());
  TEST_ASSERT_EQUAL_STRING("type_mismatch", error(0x04, R"("frame")").c_str());
  TEST_ASSERT_EQUAL_STRING("not_json", error(0x04, R"({"t":"frame","epoch":01})").c_str());
}

void TestEncodeLimits() {
  // 64 entries of the largest keys don't fit in one 2,048-byte message: the device batches.
  std::vector<ChunkKey> keys(kMaxAckEntries, ChunkKey{1, 4294967295U, 4294967295U, 4294967295U});
  std::string out(4096, '\0');
  TEST_ASSERT_EQUAL(
      0, EncodeJson(ChunkAck{keys.data(), keys.size(), nullptr, 0}, out.data(), out.size()));
  TEST_ASSERT_TRUE(EncodeJson(ChunkAck{keys.data(), 40, nullptr, 0}, out.data(), out.size()) > 0);
  TEST_ASSERT_EQUAL(
      0, EncodeJson(ChunkAck{keys.data(), kMaxAckEntries + 1, nullptr, 0}, out.data(), out.size()));
  // A buffer one byte short.
  const size_t n = EncodeJson(Flow{32, 1}, out.data(), out.size());
  TEST_ASSERT_EQUAL(0, EncodeJson(Flow{32, 1}, out.data(), n - 1));
  TEST_ASSERT_EQUAL(n, EncodeJson(Flow{32, 1}, out.data(), n));
}

void TestWriter() {
  std::string out(64, '\0');
  JsonWriter w(out.data(), out.size());
  w.BeginArray();
  w.Int(INT64_MIN);
  w.Int(-1);
  w.Uint(UINT64_MAX);
  w.String(std::string_view("\x1f\x7f/\r", 4));
  w.BeginObject();
  w.EndObject();
  w.EndArray();
  out.resize(w.Finish());
  TEST_ASSERT_EQUAL_STRING(R"([-9223372036854775808,-1,18446744073709551615,"\u001f)"
                           "\x7f"
                           R"(/\r",{}])",
                           out.c_str());
  JsonWriter open(out.data(), out.size());
  open.BeginObject();
  TEST_ASSERT_EQUAL(0, open.Finish());
}

void TestManeuverNames() {
  for (size_t i = 0; i < kManeuverCount; ++i) {
    const auto m = static_cast<Maneuver>(i);
    TEST_ASSERT_EQUAL(i, static_cast<size_t>(ManeuverFromName(ManeuverName(m))));
  }
  TEST_ASSERT_EQUAL_STRING("ferry", std::string(ManeuverName(Maneuver::kFerry)).c_str());
  TEST_ASSERT_EQUAL(static_cast<int>(Maneuver::kStraight), static_cast<int>(ManeuverFromName("")));
}

void TestReaderTokens() {
  JsonReader r;
  TEST_ASSERT_TRUE(r.Parse(R"({"a":{"b":[1,-2,{"c":true}],"s":"कx"},"n":null,"b":"top"})"));
  const int a = r.Member(kRootToken, "a");
  TEST_ASSERT_TRUE(r.IsObject(a));
  TEST_ASSERT_EQUAL(2, r.Count(a));
  const int b = r.Member(a, "b");
  TEST_ASSERT_TRUE(r.IsArray(b));
  TEST_ASSERT_EQUAL(3, r.Count(b));
  // A nested "b" is not the top-level one.
  char text[8];
  TEST_ASSERT_EQUAL(static_cast<int>(JsonGet::kOk),
                    static_cast<int>(r.String("b", text, sizeof(text))));
  TEST_ASSERT_EQUAL_STRING("top", text);
  int64_t i = 0;
  TEST_ASSERT_EQUAL(static_cast<int>(JsonGet::kOk), static_cast<int>(r.IntAt(r.Element(b, 1), &i)));
  TEST_ASSERT_EQUAL(-2, i);
  uint64_t u = 0;
  TEST_ASSERT_EQUAL(static_cast<int>(JsonGet::kWrongType),
                    static_cast<int>(r.UintAt(r.Element(b, 1), &u)));
  bool flag = false;
  TEST_ASSERT_EQUAL(static_cast<int>(JsonGet::kOk),
                    static_cast<int>(r.BoolAt(r.Member(r.Element(b, 2), "c"), &flag)));
  TEST_ASSERT_TRUE(flag);
  TEST_ASSERT_EQUAL(kNoToken, r.Element(b, 3));
  TEST_ASSERT_EQUAL(kNoToken, r.Member(b, "c"));
  TEST_ASSERT_TRUE(r.IsNull(r.Member(kRootToken, "n")));
  TEST_ASSERT_FALSE(r.IsNull(r.Member(kRootToken, "b")));
  // A UTF-8 string is measured in bytes: क is 3.
  const int s = r.Member(a, "s");
  size_t length = 0;
  TEST_ASSERT_EQUAL(static_cast<int>(JsonGet::kTooLong),
                    static_cast<int>(r.Utf8At(s, text, 4, &length)));
  TEST_ASSERT_EQUAL(static_cast<int>(JsonGet::kOk),
                    static_cast<int>(r.Utf8At(s, text, 5, &length)));
  TEST_ASSERT_EQUAL(4, length);
  TEST_ASSERT_EQUAL_STRING("\xE0\xA4\x95x", text);
  TEST_ASSERT_EQUAL(static_cast<int>(JsonGet::kWrongType),
                    static_cast<int>(r.StringAt(s, text, sizeof(text))));
  TEST_ASSERT_EQUAL(static_cast<int>(JsonGet::kMissing),
                    static_cast<int>(r.BoolAt(kNoToken, &flag)));

  // An escaped surrogate pair is one 4-byte character; a lone one is refused.
  TEST_ASSERT_TRUE(r.Parse(R"({"p":"😀","l":"\ud83d"})"));
  TEST_ASSERT_EQUAL(
      static_cast<int>(JsonGet::kOk),
      static_cast<int>(r.Utf8At(r.Member(kRootToken, "p"), text, sizeof(text), &length)));
  TEST_ASSERT_EQUAL(4, length);
  TEST_ASSERT_EQUAL(static_cast<int>(JsonGet::kWrongType),
                    static_cast<int>(r.Utf8At(r.Member(kRootToken, "l"), text, sizeof(text))));

  TEST_ASSERT_TRUE(JsonReader::IsValidJson("[1, \"x\"]"));
  TEST_ASSERT_TRUE(JsonReader::IsValidJson(" 7 "));
  TEST_ASSERT_FALSE(JsonReader::IsValidJson("[1 2]"));
  TEST_ASSERT_FALSE(JsonReader::IsValidJson("{\"a\":1,}"));
}

}  // namespace

int main() {
  UNITY_BEGIN();
  RUN_TEST(TestValidVectors);
  RUN_TEST(TestInvalidVectors);
  RUN_TEST(TestDecodedValues);
  RUN_TEST(TestDecodeRejects);
  RUN_TEST(TestEncodeLimits);
  RUN_TEST(TestWriter);
  RUN_TEST(TestManeuverNames);
  RUN_TEST(TestReaderTokens);
  return UNITY_END();
}
