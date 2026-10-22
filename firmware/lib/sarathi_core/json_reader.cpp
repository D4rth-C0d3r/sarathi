#include "json_reader.h"

#include <cstring>

// jsmn's functions are static to this file. Strict mode and parent links make the token tree
// unambiguous; the grammar itself is checked separately below, because jsmn alone accepts
// some invalid JSON (for example "[1 2]").
#define JSMN_STATIC
#define JSMN_STRICT
#define JSMN_PARENT_LINKS
#include "third_party/jsmn/jsmn.h"

namespace sarathi::core {
namespace {

using Token = JsonReader::Token;

bool IsDigit(char c) { return c >= '0' && c <= '9'; }

bool IsHex(char c) { return IsDigit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'); }

uint32_t HexValue(char c) {
  if (IsDigit(c)) {
    return static_cast<uint32_t>(c - '0');
  }
  return static_cast<uint32_t>((c | 0x20) - 'a' + 10);
}

// Strict UTF-8 (RFC 3629): no overlong forms, no surrogates, nothing above U+10FFFF.
bool ValidUtf8(std::string_view s) {
  size_t i = 0;
  while (i < s.size()) {
    const auto b0 = static_cast<uint8_t>(s[i]);
    if (b0 < 0x80) {
      ++i;
      continue;
    }
    size_t extra = 0;
    uint32_t cp = 0;
    uint32_t min = 0;
    if ((b0 & 0xE0U) == 0xC0U) {
      extra = 1;
      cp = b0 & 0x1FU;
      min = 0x80;
    } else if ((b0 & 0xF0U) == 0xE0U) {
      extra = 2;
      cp = b0 & 0x0FU;
      min = 0x800;
    } else if ((b0 & 0xF8U) == 0xF0U) {
      extra = 3;
      cp = b0 & 0x07U;
      min = 0x10000;
    } else {
      return false;
    }
    if (i + extra >= s.size()) {
      return false;  // truncated sequence
    }
    for (size_t k = 1; k <= extra; ++k) {
      const auto b = static_cast<uint8_t>(s[i + k]);
      if ((b & 0xC0U) != 0x80U) {
        return false;
      }
      cp = (cp << 6U) | (b & 0x3FU);
    }
    if (cp < min || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) {
      return false;
    }
    i += extra + 1;
  }
  return true;
}

// RFC 8259 grammar for one object, with nothing but whitespace around it. Recursion depth is
// bounded by the token limit.
class SyntaxChecker {
 public:
  explicit SyntaxChecker(std::string_view s) : s_(s) {}

  bool Run() {
    SkipSpace();
    if (Peek() != '{' || !Value(0)) {
      return false;
    }
    SkipSpace();
    return pos_ == s_.size();
  }

 private:
  [[nodiscard]] char Peek() const { return pos_ < s_.size() ? s_[pos_] : '\0'; }

  void SkipSpace() {
    while (Peek() == ' ' || Peek() == '\t' || Peek() == '\n' || Peek() == '\r') {
      ++pos_;
    }
  }

  bool Expect(char c) {
    if (Peek() != c) {
      return false;
    }
    ++pos_;
    return true;
  }

  bool Value(size_t depth) {
    if (depth > kMaxJsonTokens) {
      return false;
    }
    switch (Peek()) {
      case '{':
        return Container(depth, '}', true);
      case '[':
        return Container(depth, ']', false);
      case '"':
        return String();
      case 't':
        return Literal("true");
      case 'f':
        return Literal("false");
      case 'n':
        return Literal("null");
      default:
        return Number();
    }
  }

  // An object (members are "name": value) or an array (members are values).
  bool Container(size_t depth, char close, bool object) {
    ++pos_;
    SkipSpace();
    if (Expect(close)) {
      return true;
    }
    for (;;) {
      if (object) {
        if (!String()) {
          return false;
        }
        SkipSpace();
        if (!Expect(':')) {
          return false;
        }
        SkipSpace();
      }
      if (!Value(depth + 1)) {
        return false;
      }
      SkipSpace();
      if (Expect(close)) {
        return true;
      }
      if (!Expect(',')) {
        return false;
      }
      SkipSpace();
    }
  }

  bool String() {
    if (!Expect('"')) {
      return false;
    }
    while (pos_ < s_.size()) {
      const char c = s_[pos_];
      if (c == '"') {
        ++pos_;
        return true;
      }
      if (static_cast<uint8_t>(c) < 0x20) {
        return false;  // raw control characters must be escaped
      }
      if (c != '\\') {
        ++pos_;
        continue;
      }
      ++pos_;
      const char e = Peek();
      if (e == 'u') {
        for (size_t k = 1; k <= 4; ++k) {
          if (pos_ + k >= s_.size() || !IsHex(s_[pos_ + k])) {
            return false;
          }
        }
        pos_ += 5;
      } else if (e != '\0' && std::strchr("\"\\/bfnrt", e) != nullptr) {
        ++pos_;
      } else {
        return false;
      }
    }
    return false;
  }

  bool Literal(std::string_view word) {
    if (s_.substr(pos_, word.size()) != word) {
      return false;
    }
    pos_ += word.size();
    return true;
  }

  // -?(0|[1-9][0-9]*)(.[0-9]+)?([eE][+-]?[0-9]+)?
  bool Number() {
    Expect('-');
    if (Expect('0')) {
      // no leading zeros
    } else if (IsDigit(Peek()) && Peek() != '0') {
      while (IsDigit(Peek())) {
        ++pos_;
      }
    } else {
      return false;
    }
    if (Expect('.')) {
      if (!IsDigit(Peek())) {
        return false;
      }
      while (IsDigit(Peek())) {
        ++pos_;
      }
    }
    if (Peek() == 'e' || Peek() == 'E') {
      ++pos_;
      if (Peek() == '+' || Peek() == '-') {
        ++pos_;
      }
      if (!IsDigit(Peek())) {
        return false;
      }
      while (IsDigit(Peek())) {
        ++pos_;
      }
    }
    return true;
  }

  std::string_view s_;
  size_t pos_ = 0;
};

// Walks the characters of a JSON string's raw text (between the quotes, already checked by
// SyntaxChecker and ValidUtf8), yielding code points with escapes and surrogate pairs decoded.
class CodePoints {
 public:
  explicit CodePoints(std::string_view raw) : raw_(raw) {}

  // The next code point, or -1 at the end.
  int32_t Next() {
    if (pos_ >= raw_.size()) {
      return -1;
    }
    const auto b0 = static_cast<uint8_t>(raw_[pos_]);
    if (b0 == '\\') {
      return Escape();
    }
    if (b0 < 0x80) {
      ++pos_;
      return b0;
    }
    const size_t extra = (b0 & 0xE0U) == 0xC0U ? 1 : (b0 & 0xF0U) == 0xE0U ? 2 : 3;
    uint32_t cp = b0 & (0x3FU >> extra);
    for (size_t k = 1; k <= extra; ++k) {
      cp = (cp << 6U) | (static_cast<uint8_t>(raw_[pos_ + k]) & 0x3FU);
    }
    pos_ += extra + 1;
    return static_cast<int32_t>(cp);
  }

 private:
  uint32_t Hex4(size_t at) const {
    uint32_t v = 0;
    for (size_t k = 0; k < 4; ++k) {
      v = (v << 4U) | HexValue(raw_[at + k]);
    }
    return v;
  }

  int32_t Escape() {
    const char e = raw_[pos_ + 1];
    pos_ += 2;
    switch (e) {
      case 'b':
        return '\b';
      case 'f':
        return '\f';
      case 'n':
        return '\n';
      case 'r':
        return '\r';
      case 't':
        return '\t';
      case 'u':
        break;
      default:
        return e;  // " \ /
    }
    uint32_t cp = Hex4(pos_);
    pos_ += 4;
    // A high surrogate followed by an escaped low surrogate is one character.
    if (cp >= 0xD800 && cp <= 0xDBFF && pos_ + 6 <= raw_.size() && raw_[pos_] == '\\' &&
        raw_[pos_ + 1] == 'u') {
      const uint32_t low = Hex4(pos_ + 2);
      if (low >= 0xDC00 && low <= 0xDFFF) {
        cp = 0x10000 + ((cp - 0xD800) << 10U) + (low - 0xDC00);
        pos_ += 6;
      }
    }
    return static_cast<int32_t>(cp);
  }

  std::string_view raw_;
  size_t pos_ = 0;
};

std::string_view RawText(std::string_view json, const Token& t) {
  return json.substr(static_cast<size_t>(t.start), static_cast<size_t>(t.end - t.start));
}

bool SameString(std::string_view a, std::string_view b) {
  CodePoints x(a);
  CodePoints y(b);
  for (;;) {
    const int32_t cx = x.Next();
    if (cx != y.Next()) {
      return false;
    }
    if (cx < 0) {
      return true;
    }
  }
}

}  // namespace

bool JsonReader::Parse(std::string_view json) {
  count_ = 0;
  json_ = json;
  if (!ValidUtf8(json) || !SyntaxChecker(json).Run()) {
    return false;
  }
  std::array<jsmntok_t, kMaxJsonTokens> raw{};
  jsmn_parser parser;
  jsmn_init(&parser);
  const int n = jsmn_parse(&parser, json.data(), json.size(), raw.data(), kMaxJsonTokens);
  if (n < 1 || raw[0].type != JSMN_OBJECT) {
    return false;  // includes JSMN_ERROR_NOMEM: more than kMaxJsonTokens tokens
  }
  for (int i = 0; i < n; ++i) {
    const jsmntok_t& t = raw.at(static_cast<size_t>(i));
    tokens_.at(static_cast<size_t>(i)) =
        Token{static_cast<int>(t.type), t.start, t.end, t.size, t.parent};
  }
  // Duplicate member names, in every object. The members of object o are the tokens whose
  // parent is o.
  for (int o = 0; o < n; ++o) {
    if (tokens_.at(static_cast<size_t>(o)).type != JSMN_OBJECT) {
      continue;
    }
    for (int a = o + 1; a < n; ++a) {
      if (tokens_.at(static_cast<size_t>(a)).parent != o) {
        continue;
      }
      for (int b = a + 1; b < n; ++b) {
        if (tokens_.at(static_cast<size_t>(b)).parent == o &&
            SameString(RawText(json, tokens_.at(static_cast<size_t>(a))),
                       RawText(json, tokens_.at(static_cast<size_t>(b))))) {
          return false;
        }
      }
    }
  }
  count_ = n;
  return true;
}

int JsonReader::FindValue(std::string_view name) const {
  for (int i = 1; i + 1 < count_; ++i) {
    const Token& t = tokens_.at(static_cast<size_t>(i));
    if (t.parent == 0 && t.type == JSMN_STRING && SameString(RawText(json_, t), name)) {
      return i + 1;  // jsmn puts a member's value right after its name
    }
  }
  return -1;
}

bool JsonReader::Has(std::string_view name) const { return FindValue(name) >= 0; }

JsonGet JsonReader::String(std::string_view name, char* out, size_t capacity,
                           size_t* length) const {
  const int index = FindValue(name);
  if (index < 0) {
    return JsonGet::kMissing;
  }
  const Token& t = tokens_.at(static_cast<size_t>(index));
  if (t.type != JSMN_STRING) {
    return JsonGet::kWrongType;
  }
  // First the whole value must be ASCII without NUL; only then does its length matter.
  size_t n = 0;
  CodePoints check(RawText(json_, t));
  for (int32_t cp = check.Next(); cp >= 0; cp = check.Next()) {
    if (cp == 0 || cp > 0x7F) {
      return JsonGet::kWrongType;
    }
    ++n;
  }
  if (n + 1 > capacity) {
    return JsonGet::kTooLong;
  }
  CodePoints copy(RawText(json_, t));
  size_t i = 0;
  for (int32_t cp = copy.Next(); cp >= 0; cp = copy.Next()) {
    out[i++] = static_cast<char>(cp);  // NOLINT(cppcoreguidelines-pro-bounds-pointer-arithmetic)
  }
  out[i] = '\0';  // NOLINT(cppcoreguidelines-pro-bounds-pointer-arithmetic)
  if (length != nullptr) {
    *length = i;
  }
  return JsonGet::kOk;
}

JsonGet JsonReader::Uint(std::string_view name, uint64_t* out) const {
  const int index = FindValue(name);
  if (index < 0) {
    return JsonGet::kMissing;
  }
  const Token& t = tokens_.at(static_cast<size_t>(index));
  const std::string_view text = RawText(json_, t);
  if (t.type != JSMN_PRIMITIVE || text.empty() || (text.size() > 1 && text[0] == '0')) {
    return JsonGet::kWrongType;
  }
  uint64_t value = 0;
  for (const char c : text) {
    if (!IsDigit(c)) {
      return JsonGet::kWrongType;  // sign, fraction, exponent, true, false, null
    }
    value = (value * 10) + static_cast<uint64_t>(c - '0');
    if (value > kMaxJsonInteger) {
      return JsonGet::kWrongType;
    }
  }
  *out = value;
  return JsonGet::kOk;
}

}  // namespace sarathi::core
