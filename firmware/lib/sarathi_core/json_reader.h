// Allocation-free reader for one JSON object, with jsmn as the tokenizer. Strict where the OTA
// manifest spec (docs/ota/manifest.md) needs it, matching Python's json module:
//   - valid UTF-8, one object, nothing after it
//   - no duplicate member names in any object (compared after unescaping)
//   - no trailing commas, no raw control characters in strings, no NaN or Infinity
//   - at most kMaxJsonTokens tokens (one per value and one per member name)
// Lookups are for top-level members. The reader keeps a view of the parsed text, which must
// outlive it.

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
  kWrongType,  // not this type; for strings also not ASCII or containing NUL
  kTooLong,    // an ASCII string longer than the caller's buffer
};

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

  std::array<Token, kMaxJsonTokens> tokens_{};
  int count_ = 0;
  std::string_view json_;
};

}  // namespace sarathi::core
