// Allocation-free reader for one JSON object, with jsmn as the tokenizer. Strict where the OTA
// manifest spec (docs/ota/manifest.md) needs it, matching Python's json module:
//   - valid UTF-8, one object, nothing after it
//   - no duplicate member names in any object (compared after unescaping)
//   - no trailing commas, no raw control characters in strings, no NaN or Infinity
//   - at most kMaxJsonTokens tokens (one per value and one per member name)
// Lookups by name are for top-level members; the token-level accessors reach nested values.
// The reader keeps a view of the parsed text, which must outlive it.

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace sarathi::core {

constexpr size_t kMaxJsonTokens = 128;
constexpr uint64_t kMaxJsonInteger = 1ULL << 53U;

enum class JsonGet : uint8_t {
  kOk,
  kMissing,
  kWrongType,  // not this type; for strings also NUL, a lone surrogate, or (String) not ASCII
  kTooLong,    // a string longer than the caller's buffer
};

// A token index from Member or Element; kNoToken (missing) makes every accessor return
// kMissing.
constexpr int kRootToken = 0;
constexpr int kNoToken = -1;

class JsonReader {
 public:
  [[nodiscard]] bool Parse(std::string_view json);

  [[nodiscard]] bool Has(std::string_view name) const;

  // An ASCII string member, unescaped into `out` and NUL-terminated (`capacity` includes the
  // terminator).
  [[nodiscard]] JsonGet String(std::string_view name, char* out, size_t capacity,
                               size_t* length = nullptr) const;

  // A whole number from 0 to kMaxJsonInteger, written without sign, fraction or exponent.
  // true and false are not numbers.
  [[nodiscard]] JsonGet Uint(std::string_view name, uint64_t* out) const;

  // ---- Token-level access (kRootToken is the top-level object).

  // The value of member `name` of `object`, or kNoToken.
  [[nodiscard]] int Member(int object, std::string_view name) const;
  // Element `index` of `array`, or kNoToken.
  [[nodiscard]] int Element(int array, int index) const;
  // Elements of an array or members of an object; 0 for anything else.
  [[nodiscard]] int Count(int container) const;
  [[nodiscard]] bool IsObject(int token) const;
  [[nodiscard]] bool IsArray(int token) const;
  [[nodiscard]] bool IsNull(int token) const;

  [[nodiscard]] JsonGet StringAt(int token, char* out, size_t capacity,
                                 size_t* length = nullptr) const;  // ASCII, as String
  // Any UTF-8 string without NUL, unescaped into `out` and NUL-terminated; `capacity`
  // includes the terminator, so it is the byte limit plus one.
  [[nodiscard]] JsonGet Utf8At(int token, char* out, size_t capacity,
                               size_t* length = nullptr) const;
  [[nodiscard]] JsonGet UintAt(int token, uint64_t* out) const;  // as Uint
  // A whole number from -kMaxJsonInteger to kMaxJsonInteger, without fraction or exponent.
  [[nodiscard]] JsonGet IntAt(int token, int64_t* out) const;
  [[nodiscard]] JsonGet BoolAt(int token, bool* out) const;

  // True if `json` is one valid JSON value of any kind (same strictness as Parse, without its
  // object, duplicate-name and token-count rules).
  [[nodiscard]] static bool IsValidJson(std::string_view json);

  // jsmn's token, copied out so this header needn't include jsmn.
  struct Token {
    int type = 0;
    int start = 0;
    int end = 0;
    int size = 0;
    int parent = -1;
  };

 private:
  [[nodiscard]] int FindValue(std::string_view name) const;  // token index, or -1
  [[nodiscard]] const Token* At(int token) const;            // nullptr if out of range

  std::array<Token, kMaxJsonTokens> tokens_{};
  int count_ = 0;
  std::string_view json_;
};

}  // namespace sarathi::core
