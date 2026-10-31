// Reads the protocol golden vectors (docs/protocol/vectors) for the native tests. Header-only,
// included by each test_proto_* suite. The files are larger than JsonReader's limits, so this
// uses jsmn directly with a large host-only token pool.

#pragma once

#include <cstdint>
#include <cstdio>
#include <string>
#include <string_view>
#include <vector>

#include <unity.h>

#define JSMN_STATIC
#define JSMN_PARENT_LINKS
#include "third_party/jsmn/jsmn.h"

namespace protocol_vectors {

inline std::string ReadFile(const std::string& path) {
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

struct Doc {
  std::string text;
  std::vector<jsmntok_t> tokens;
};

inline Doc Load(std::string_view name) {
  const std::string path = std::string(SARATHI_PROTOCOL_VECTORS_DIR) + "/" + std::string(name);
  Doc doc{ReadFile(path), std::vector<jsmntok_t>(65536)};
  jsmn_parser parser;
  jsmn_init(&parser);
  const int n = jsmn_parse(&parser, doc.text.data(), doc.text.size(), doc.tokens.data(),
                           static_cast<unsigned>(doc.tokens.size()));
  TEST_ASSERT_TRUE_MESSAGE(n > 0, path.c_str());
  doc.tokens.resize(static_cast<size_t>(n));
  return doc;
}

inline std::string Text(const Doc& doc, int i) {
  const jsmntok_t& t = doc.tokens.at(static_cast<size_t>(i));
  return doc.text.substr(static_cast<size_t>(t.start), static_cast<size_t>(t.end - t.start));
}

// The value of member `name` of object `object`, or -1.
inline int Member(const Doc& doc, int object, const char* name) {
  for (int i = object + 1; i + 1 < static_cast<int>(doc.tokens.size()); ++i) {
    if (doc.tokens.at(static_cast<size_t>(i)).parent == object && Text(doc, i) == name) {
      return i + 1;
    }
  }
  return -1;
}

// Like Member, but the vector must have it.
inline int Need(const Doc& doc, int object, const char* name) {
  const int i = Member(doc, object, name);
  TEST_ASSERT_TRUE_MESSAGE(i >= 0, name);
  return i;
}

inline std::vector<int> Elements(const Doc& doc, int array) {
  std::vector<int> out;
  for (int i = array + 1; i < static_cast<int>(doc.tokens.size()); ++i) {
    if (doc.tokens.at(static_cast<size_t>(i)).parent == array) {
      out.push_back(i);
    }
  }
  return out;
}

// A string token's value: JSON escapes decoded, as UTF-8.
inline std::string String(const Doc& doc, int i) {
  const std::string raw = Text(doc, i);
  std::string out;
  auto put = [&out](uint32_t cp) {
    if (cp < 0x80) {
      out += static_cast<char>(cp);
    } else if (cp < 0x800) {
      out += static_cast<char>(0xC0 | (cp >> 6));
      out += static_cast<char>(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
      out += static_cast<char>(0xE0 | (cp >> 12));
      out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
      out += static_cast<char>(0x80 | (cp & 0x3F));
    } else {
      out += static_cast<char>(0xF0 | (cp >> 18));
      out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
      out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
      out += static_cast<char>(0x80 | (cp & 0x3F));
    }
  };
  for (size_t k = 0; k < raw.size(); ++k) {
    if (raw[k] != '\\') {
      out += raw[k];
      continue;
    }
    const char e = raw[++k];
    switch (e) {
      case 'b':
        out += '\b';
        break;
      case 'f':
        out += '\f';
        break;
      case 'n':
        out += '\n';
        break;
      case 'r':
        out += '\r';
        break;
      case 't':
        out += '\t';
        break;
      case 'u': {
        uint32_t cp = static_cast<uint32_t>(std::stoul(raw.substr(k + 1, 4), nullptr, 16));
        k += 4;
        if (cp >= 0xD800 && cp <= 0xDBFF && k + 6 < raw.size() && raw[k + 1] == '\\') {
          const auto low = static_cast<uint32_t>(std::stoul(raw.substr(k + 3, 4), nullptr, 16));
          cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
          k += 6;
        }
        put(cp);
        break;
      }
      default:
        out += e;  // " \ /
    }
  }
  return out;
}

inline long Integer(const Doc& doc, int i) { return std::stol(Text(doc, i), nullptr, 0); }

inline std::vector<uint8_t> Hex(const std::string& hex) {
  TEST_ASSERT_EQUAL(0, hex.size() % 2);
  std::vector<uint8_t> out(hex.size() / 2);
  for (size_t i = 0; i < out.size(); ++i) {
    out.at(i) = static_cast<uint8_t>(std::stoul(hex.substr(2 * i, 2), nullptr, 16));
  }
  return out;
}

}  // namespace protocol_vectors
