// Writes compact JSON into a fixed buffer, in the canonical form of protocol.md section 3:
// no whitespace, plain decimal integers, raw UTF-8 strings escaping only ", \ and U+0000 to
// U+001F. Keys come out in the order they are written.

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace sarathi::proto {

class JsonWriter {
 public:
  JsonWriter(char* out, size_t capacity) : out_(out), capacity_(capacity) {}

  void BeginObject() { Open('{'); }
  void EndObject() { Close('}'); }
  void BeginArray() { Open('['); }
  void EndArray() { Close(']'); }
  void Key(std::string_view name);
  void String(std::string_view text);
  void Uint(uint64_t value);
  void Int(int64_t value);
  void Bool(bool value) { Value(value ? "true" : "false"); }
  void Null() { Value("null"); }

  // The length written, or 0 if it didn't fit or the nesting is unbalanced.
  [[nodiscard]] size_t Finish() const { return ok_ && depth_ == 0 ? size_ : 0; }

 private:
  static constexpr size_t kMaxDepth = 8;

  void Separator();  // a comma before every element or member but the first
  void Open(char c);
  void Close(char c);
  void Value(std::string_view raw);
  void Put(char c);
  void Put(std::string_view s);

  char* out_;
  size_t capacity_;
  size_t size_ = 0;
  bool ok_ = true;
  size_t depth_ = 0;
  std::array<bool, kMaxDepth> has_items_{};
  bool after_key_ = false;
};

}  // namespace sarathi::proto
