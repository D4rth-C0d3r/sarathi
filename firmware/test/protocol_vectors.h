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
