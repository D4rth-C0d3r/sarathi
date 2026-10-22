// OTA manifest verification against the golden vectors (docs/ota/vectors), plus unit tests
// for its parts. ES256 can't run natively, so a fake verifier answers with each vector's
// recorded signature_valid; real ES256 runs on the device against the same vectors.

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <unity.h>

#include "base64url.h"
#include "json_reader.h"
#include "ota_manifest.h"
#include "ota_version.h"
#include "version.h"

// The test reads vectors.json with jsmn directly: it is larger than JsonReader's limits.
#define JSMN_STATIC
#define JSMN_PARENT_LINKS
#include "third_party/jsmn/jsmn.h"

using sarathi::core::Base64UrlDecode;
using sarathi::core::ImageCheck;
using sarathi::core::JsonGet;
using sarathi::core::JsonReader;
using sarathi::core::kFirmwareSeq;
using sarathi::core::kInvalidSeq;
using sarathi::core::kMaxJsonInteger;
using sarathi::core::Manifest;
using sarathi::core::ManifestContext;
using sarathi::core::ManifestScratch;
using sarathi::core::OtaError;
using sarathi::core::OtaErrorCode;
using sarathi::core::SeqForVersion;
using sarathi::core::Sha256;
using sarathi::core::SignatureVerifier;

// Unity calls these by name, so they need external linkage and Unity's spelling.
void setUp() {}     // NOLINT(misc-use-internal-linkage,readability-identifier-naming)
void tearDown() {}  // NOLINT(misc-use-internal-linkage,readability-identifier-naming)

namespace {

std::string VectorPath(std::string_view name) {
  return std::string(SARATHI_OTA_VECTORS_DIR) + "/" + std::string(name);
}

std::string ReadFile(const std::string& path) {
  std::string data;
  std::FILE* f = std::fopen(path.c_str(), "rb");
  TEST_ASSERT_NOT_NULL_MESSAGE(f, path.c_str());
  char buffer[4096];
  size_t n = 0;
  while ((n = std::fread(buffer, 1, sizeof(buffer), f)) > 0) {
    data.append(buffer, n);
  }
  std::fclose(f);
  return data;
}

// ---- A minimal view of vectors.json.

struct Doc {
  std::string text;
  std::vector<jsmntok_t> tokens;
};

Doc LoadDoc(const std::string& path) {
  Doc doc{ReadFile(path), std::vector<jsmntok_t>(8192)};
  jsmn_parser parser;
  jsmn_init(&parser);
  const int n = jsmn_parse(&parser, doc.text.data(), doc.text.size(), doc.tokens.data(),
                           static_cast<unsigned>(doc.tokens.size()));
  TEST_ASSERT_TRUE_MESSAGE(n > 0, "vectors.json does not parse");
  doc.tokens.resize(static_cast<size_t>(n));
  return doc;
}

std::string Text(const Doc& doc, int i) {
  const jsmntok_t& t = doc.tokens.at(static_cast<size_t>(i));
  return doc.text.substr(static_cast<size_t>(t.start), static_cast<size_t>(t.end - t.start));
}

// The value of member `name` of object `object`, or -1.
int Member(const Doc& doc, int object, const char* name) {
  for (int i = object + 1; i + 1 < static_cast<int>(doc.tokens.size()); ++i) {
    if (doc.tokens.at(static_cast<size_t>(i)).parent == object && Text(doc, i) == name) {
      return i + 1;
    }
  }
  return -1;
}

std::vector<int> Elements(const Doc& doc, int array) {
  std::vector<int> out;
  for (int i = array + 1; i < static_cast<int>(doc.tokens.size()); ++i) {
    if (doc.tokens.at(static_cast<size_t>(i)).parent == array) {
      out.push_back(i);
    }
  }
  return out;
}

Sha256 FromHex(const std::string& hex) {
  Sha256 out{};
  for (size_t i = 0; i < out.size(); ++i) {
    out.at(i) = static_cast<uint8_t>(std::stoul(hex.substr(2 * i, 2), nullptr, 16));
  }
  return out;
}

// Answers with the vector's recorded signature_valid, and counts calls.
class FakeVerifier : public SignatureVerifier {
 public:
  FakeVerifier(std::string kid, int signature_valid)
      : kid_(std::move(kid)), signature_valid_(signature_valid) {}

  [[nodiscard]] bool Trusts(std::string_view kid) const override { return kid == kid_; }

  [[nodiscard]] bool Verify(std::string_view kid, std::string_view signing_input,
                            const uint8_t* /*signature*/) const override {
    ++calls_;
    TEST_ASSERT_TRUE(kid == kid_);
    TEST_ASSERT_EQUAL(2, std::count(signing_input.begin(), signing_input.end(), '.') + 1);
    return signature_valid_ == 1;
  }

  [[nodiscard]] int Calls() const { return calls_; }

 private:
  std::string kid_;
  int signature_valid_;  // 1, 0, or -1 for null (verification must not get that far)
  mutable int calls_ = 0;
};

ManifestScratch scratch;  // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)

void TestGoldenVectors() {
  const Doc doc = LoadDoc(VectorPath("vectors.json"));
  const int context = Member(doc, 0, "context");
  const std::string board = Text(doc, Member(doc, context, "board"));
  const std::string kid = Text(doc, Member(doc, context, "kid"));
  const ManifestContext ctx{
      board, static_cast<uint32_t>(std::stoul(Text(doc, Member(doc, context, "running_seq")))),
      std::stoull(Text(doc, Member(doc, context, "slot_size")))};

  const std::vector<int> cases = Elements(doc, Member(doc, 0, "cases"));
  TEST_ASSERT_TRUE(cases.size() >= 26);
  for (const int c : cases) {
    const std::string name = Text(doc, Member(doc, c, "name"));
    const std::string expect = Text(doc, Member(doc, c, "expect"));
    const std::string sig = Text(doc, Member(doc, c, "signature_valid"));
    const int signature_valid = sig == "true" ? 1 : sig == "false" ? 0 : -1;

    FakeVerifier verifier(kid, signature_valid);
    Manifest manifest;
    const std::string token = ReadFile(VectorPath(Text(doc, Member(doc, c, "manifest"))));
    OtaError error = VerifyManifest(token, ctx, verifier, scratch, &manifest);

    const int image = Member(doc, c, "image");
    if (image >= 0 && error == OtaError::kNone) {
      const std::string bytes = ReadFile(VectorPath(Text(doc, image)));
      ImageCheck check(manifest.size, manifest.sha256);
      static_cast<void>(check.Add(bytes.size()));
      error = check.Finish(FromHex(Text(doc, Member(doc, c, "image_sha256"))));
    }
    TEST_ASSERT_EQUAL_STRING_MESSAGE(expect.c_str(), OtaErrorCode(error), name.c_str());
    TEST_ASSERT_EQUAL_MESSAGE(signature_valid < 0 ? 0 : 1, verifier.Calls(), name.c_str());

    if (name == "valid") {
      TEST_ASSERT_EQUAL_STRING("crowpanel21", manifest.board.CStr());
      TEST_ASSERT_EQUAL_STRING("0.3.0", manifest.version.CStr());
      TEST_ASSERT_EQUAL_STRING(kid.c_str(), manifest.kid.CStr());
      TEST_ASSERT_EQUAL_UINT32(300, manifest.seq);
      TEST_ASSERT_EQUAL_UINT64(4096, manifest.size);
      TEST_ASSERT_EQUAL_UINT32(1, manifest.min_protocol);
      TEST_ASSERT_TRUE(manifest.url.View().substr(0, 8) == "https://");
    }
  }
}

void TestBase64Url() {
  uint8_t out[8];
  size_t n = 99;
  TEST_ASSERT_TRUE(Base64UrlDecode("", out, sizeof(out), &n));
  TEST_ASSERT_EQUAL(0, n);
  TEST_ASSERT_TRUE(Base64UrlDecode("TWFu", out, sizeof(out), &n));
  TEST_ASSERT_EQUAL(3, n);
  TEST_ASSERT_EQUAL_MEMORY("Man", out, 3);
  TEST_ASSERT_TRUE(Base64UrlDecode("TWE", out, sizeof(out), &n));
  TEST_ASSERT_EQUAL(2, n);
  TEST_ASSERT_TRUE(Base64UrlDecode("TQ", out, sizeof(out), &n));
  TEST_ASSERT_EQUAL(1, n);
  TEST_ASSERT_EQUAL('M', out[0]);
  TEST_ASSERT_TRUE(Base64UrlDecode("-_-_", out, sizeof(out), &n));  // URL-safe alphabet
  const uint8_t url_safe[] = {0xFB, 0xFF, 0xBF};
  TEST_ASSERT_EQUAL_MEMORY(url_safe, out, 3);

  TEST_ASSERT_FALSE(Base64UrlDecode("T", out, sizeof(out), &n));             // 4n + 1
  TEST_ASSERT_FALSE(Base64UrlDecode("TQ==", out, sizeof(out), &n));          // padding
  TEST_ASSERT_FALSE(Base64UrlDecode("+/+/", out, sizeof(out), &n));          // standard alphabet
  TEST_ASSERT_FALSE(Base64UrlDecode("TWFuTWFuTWFu", out, sizeof(out), &n));  // 9 > 8 bytes
}

void TestSeqForVersion() {
  TEST_ASSERT_EQUAL_INT64(0, SeqForVersion("0.0.0"));
  TEST_ASSERT_EQUAL_INT64(300, SeqForVersion("0.3.0"));
  TEST_ASSERT_EQUAL_INT64(10203, SeqForVersion("1.2.3"));
  TEST_ASSERT_EQUAL_INT64(9999999, SeqForVersion("999.99.99"));
  for (const char* bad : {"", "1", "1.0", "1.0.0.0", "01.0.0", "1.00.0", "1.100.0", "1000.0.0",
                          "1.0.0-rc1", "a.b.c", "1..0", "-1.0.0"}) {
    TEST_ASSERT_EQUAL_INT64_MESSAGE(kInvalidSeq, SeqForVersion(bad), bad);
  }
  TEST_ASSERT_EQUAL_UINT32(0, kFirmwareSeq);  // local builds are 0.0.0-dev
}

bool Parses(std::string_view json) {
  JsonReader reader;
  return reader.Parse(json);
}

void TestJsonStructure() {
  TEST_ASSERT_TRUE(Parses(R"({"a":"x","n":5,"o":{"k":[1,2,{"z":null}]},"b":true})"));
  TEST_ASSERT_TRUE(Parses(" \t\r\n{ \"a\" : 1 } \n"));
  TEST_ASSERT_TRUE(Parses(R"({"k":1,"o":{"k":2}})"));  // same name in different objects

  TEST_ASSERT_FALSE(Parses(R"({"a":1,"a":2})"));
  TEST_ASSERT_FALSE(Parses(R"({"a":1,"a":2})"));  // the same name after unescaping
  TEST_ASSERT_FALSE(Parses(R"({"o":{"k":1,"k":2}})"));
  TEST_ASSERT_FALSE(Parses(R"({"a":1,})"));
  TEST_ASSERT_FALSE(Parses(R"({"a":[1,]})"));
  TEST_ASSERT_FALSE(Parses(R"({"a":[1 2]})"));
  TEST_ASSERT_FALSE(Parses(R"({"a" 1})"));
  TEST_ASSERT_FALSE(Parses(R"({"a":NaN})"));
  TEST_ASSERT_FALSE(Parses(R"({"a":Infinity})"));
  TEST_ASSERT_FALSE(Parses(R"({"a":01})"));
  TEST_ASSERT_FALSE(Parses(R"({"a":1.})"));
  TEST_ASSERT_FALSE(Parses("{\"a\":\"x\ty\"}"));          // raw control character
  TEST_ASSERT_FALSE(Parses(R"({"a":"\x"})"));             // unknown escape
  TEST_ASSERT_FALSE(Parses("{\"a\":\"\xC3\"}"));          // truncated UTF-8
  TEST_ASSERT_FALSE(Parses("{\"a\":\"\xC0\xAF\"}"));      // overlong UTF-8
  TEST_ASSERT_FALSE(Parses("{\"a\":\"\xED\xA0\x80\"}"));  // encoded surrogate
  TEST_ASSERT_FALSE(Parses("[1]"));
  TEST_ASSERT_FALSE(Parses("{} {}"));
  TEST_ASSERT_FALSE(Parses("{"));
  TEST_ASSERT_FALSE(Parses(""));
}

std::string ObjectWithMembers(int members) {
  std::string json = "{";
  for (int i = 0; i < members; ++i) {
    json += (i == 0 ? "\"m" : ",\"m") + std::to_string(i) + "\":0";
  }
  return json + "}";
}

void TestJsonTokenLimit() {
  TEST_ASSERT_TRUE(Parses(ObjectWithMembers(63)));   // 1 + 2*63 = 127 tokens
  TEST_ASSERT_FALSE(Parses(ObjectWithMembers(64)));  // 129
}

void TestJsonValues() {
  JsonReader r;
  TEST_ASSERT_TRUE(
      r.Parse(R"({"s":"a\/b\"cA","n":0,"big":9007199254740992,"over":9007199254740993,)"
              R"("neg":-1,"frac":1.0,"exp":1e3,"t":true,"str5":"5","e":"é","raw":"é",)"
              R"("nul":"\u0000","long":"abcdefgh","long_e":"abcdefgé"})"));
  char buf[8];
  size_t len = 0;
  TEST_ASSERT_EQUAL(JsonGet::kOk, r.String("s", buf, sizeof(buf), &len));
  TEST_ASSERT_EQUAL_STRING("a/b\"cA", buf);
  TEST_ASSERT_EQUAL(6, len);
  TEST_ASSERT_EQUAL(JsonGet::kMissing, r.String("none", buf, sizeof(buf)));
  TEST_ASSERT_EQUAL(JsonGet::kWrongType, r.String("n", buf, sizeof(buf)));
  TEST_ASSERT_EQUAL(JsonGet::kWrongType, r.String("e", buf, sizeof(buf)));
  TEST_ASSERT_EQUAL(JsonGet::kWrongType, r.String("raw", buf, sizeof(buf)));
  TEST_ASSERT_EQUAL(JsonGet::kWrongType, r.String("nul", buf, sizeof(buf)));
  TEST_ASSERT_EQUAL(JsonGet::kTooLong, r.String("long", buf, sizeof(buf)));      // 8 + NUL > 8
  TEST_ASSERT_EQUAL(JsonGet::kWrongType, r.String("long_e", buf, sizeof(buf)));  // ASCII first

  uint64_t v = 0;
  TEST_ASSERT_EQUAL(JsonGet::kOk, r.Uint("n", &v));
  TEST_ASSERT_EQUAL_UINT64(0, v);
  TEST_ASSERT_EQUAL(JsonGet::kOk, r.Uint("big", &v));
  TEST_ASSERT_EQUAL_UINT64(kMaxJsonInteger, v);
  for (const char* bad : {"over", "neg", "frac", "exp", "t", "str5", "s"}) {
    TEST_ASSERT_EQUAL_MESSAGE(JsonGet::kWrongType, r.Uint(bad, &v), bad);
  }
  TEST_ASSERT_EQUAL(JsonGet::kMissing, r.Uint("none", &v));
  TEST_ASSERT_TRUE(r.Has("t"));
  TEST_ASSERT_FALSE(r.Has("a"));  // only member names count, not values
}

void TestImageCheck() {
  Sha256 digest{};
  digest.at(0) = 0xAB;
  ImageCheck ok(10, digest);
  TEST_ASSERT_TRUE(ok.Add(4));
  TEST_ASSERT_TRUE(ok.Add(6));
  TEST_ASSERT_EQUAL_UINT64(10, ok.Received());
  TEST_ASSERT_EQUAL(OtaError::kNone, ok.Finish(digest));
  TEST_ASSERT_EQUAL(OtaError::kHashMismatch, ok.Finish(Sha256{}));

  ImageCheck over(10, digest);
  TEST_ASSERT_FALSE(over.Add(11));  // stop the download as soon as it overruns
  TEST_ASSERT_EQUAL(OtaError::kSizeMismatch, over.Finish(digest));

  ImageCheck short_image(10, digest);
  TEST_ASSERT_TRUE(short_image.Add(9));
  TEST_ASSERT_EQUAL(OtaError::kSizeMismatch, short_image.Finish(digest));
}

void TestErrorCodes() {
  TEST_ASSERT_EQUAL_STRING("ok", OtaErrorCode(OtaError::kNone));
  TEST_ASSERT_EQUAL_STRING("unsupported_type", OtaErrorCode(OtaError::kUnsupportedType));
  TEST_ASSERT_EQUAL_STRING("hash_mismatch", OtaErrorCode(OtaError::kHashMismatch));
}

}  // namespace

int main() {
  UNITY_BEGIN();
  RUN_TEST(TestGoldenVectors);
  RUN_TEST(TestBase64Url);
  RUN_TEST(TestSeqForVersion);
  RUN_TEST(TestJsonStructure);
  RUN_TEST(TestJsonTokenLimit);
  RUN_TEST(TestJsonValues);
  RUN_TEST(TestImageCheck);
  RUN_TEST(TestErrorCodes);
  return UNITY_END();
}
