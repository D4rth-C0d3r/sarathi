// JSON messages (docs/protocol/protocol.md section 3): decoding what the device receives,
// validated as strictly as the schemas in docs/protocol/schema, and the canonical encoding of
// every JSON message (both directions, so tests can compare bytes with the golden vectors).

#pragma once

#include <cstddef>
#include <cstdint>
#include <variant>

#include "json_reader.h"
#include "messages.h"

namespace sarathi::proto {

// Why a JSON message was dropped; the names are the vectors' `error` values.
enum class JsonError : uint8_t {
  kNone,
  kUnknownType,  // not a phone → device JSON type: ignored (protocol.md section 6)
  kTooLong,
  kNotJson,
  kTypeMismatch,
  kInvalid,
};

[[nodiscard]] const char* JsonErrorName(JsonError error);

using PhoneMessage = std::variant<HelloPhone, Time, PhoneBatt, FrameEpoch, Backend, Event, Nav,
                                  NavEnd, Call, Media, Lock>;

// Holds the tokenizer (a few KB), so it is meant to be allocated once, not on a small stack.
class PhoneJsonDecoder {
 public:
  [[nodiscard]] JsonError Decode(uint8_t type, const uint8_t* data, size_t size, PhoneMessage* out);

 private:
  core::JsonReader reader_;
};

// Canonical encodings. Each returns the length written to `out` (no terminator), or 0 if it
// needs more than `capacity` or kMaxJson bytes.
size_t EncodeJson(const HelloPhone& m, char* out, size_t capacity);
size_t EncodeJson(const Time& m, char* out, size_t capacity);
size_t EncodeJson(const PhoneBatt& m, char* out, size_t capacity);
size_t EncodeJson(const FrameEpoch& m, char* out, size_t capacity);
size_t EncodeJson(const Backend& m, char* out, size_t capacity);
size_t EncodeJson(const Event& m, char* out, size_t capacity);
size_t EncodeJson(const Nav& m, char* out, size_t capacity);
size_t EncodeJson(const NavEnd& m, char* out, size_t capacity);
size_t EncodeJson(const Call& m, char* out, size_t capacity);
size_t EncodeJson(const Media& m, char* out, size_t capacity);
size_t EncodeJson(const Lock& m, char* out, size_t capacity);
size_t EncodeJson(const HelloDevice& m, char* out, size_t capacity);
size_t EncodeJson(const ChunkAck& m, char* out, size_t capacity);
size_t EncodeJson(const Flow& m, char* out, size_t capacity);
size_t EncodeJson(const Style& m, char* out, size_t capacity);
size_t EncodeJson(const CallAction& m, char* out, size_t capacity);
size_t EncodeJson(const MediaCmd& m, char* out, size_t capacity);

}  // namespace sarathi::proto
