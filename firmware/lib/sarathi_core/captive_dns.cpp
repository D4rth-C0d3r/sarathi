#include "captive_dns.h"

#include <cstring>

namespace sarathi::core {
namespace {

constexpr size_t kHeaderBytes = 12;
constexpr size_t kAnswerBytes = 16;  // name pointer, type, class, TTL, length, IPv4
constexpr uint16_t kTypeA = 1;
constexpr uint16_t kClassIn = 1;
constexpr uint32_t kTtlSeconds = 60;
constexpr uint8_t kMaxLabel = 63;

uint16_t Read16(const uint8_t* p) {
  return static_cast<uint16_t>((p[0] << 8U) | p[1]);  // NOLINT(*-pointer-arithmetic)
}

void Write16(uint8_t* p, uint16_t v) {
  p[0] = static_cast<uint8_t>(v >> 8U);    // NOLINT(*-pointer-arithmetic)
  p[1] = static_cast<uint8_t>(v & 0xFFU);  // NOLINT(*-pointer-arithmetic)
}

}  // namespace

size_t BuildCaptiveDnsReply(const uint8_t* query, size_t size, const std::array<uint8_t, 4>& ip,
                            uint8_t* out, size_t capacity) {
  if (size < kHeaderBytes || size > kDnsMaxPacket) {
    return 0;
  }
  const uint16_t flags = Read16(query + 2);                // NOLINT(*-pointer-arithmetic)
  const bool is_query = (flags & 0x8000U) == 0;            // QR = 0
  const bool standard = ((flags >> 11U) & 0x0FU) == 0;     // OPCODE = QUERY
  if (!is_query || !standard || Read16(query + 4) != 1) {  // NOLINT(*-pointer-arithmetic)
    return 0;
  }
  // Walk the question's name: labels up to a zero byte, no compression in questions.
  size_t pos = kHeaderBytes;
  while (pos < size && query[pos] != 0) {  // NOLINT(*-pointer-arithmetic)
    const uint8_t label = query[pos];      // NOLINT(*-pointer-arithmetic)
    if (label > kMaxLabel) {
      return 0;
    }
    pos += 1 + label;
  }
  const size_t question_end = pos + 1 + 4;  // zero byte, QTYPE, QCLASS
  if (question_end > size) {
    return 0;
  }
  const uint16_t qtype = Read16(query + pos + 1);   // NOLINT(*-pointer-arithmetic)
  const uint16_t qclass = Read16(query + pos + 3);  // NOLINT(*-pointer-arithmetic)
  const bool answer = qtype == kTypeA && qclass == kClassIn;
  const size_t reply_size = question_end + (answer ? kAnswerBytes : 0);
  if (reply_size > capacity) {
    return 0;
  }

  std::memcpy(out, query, question_end);  // ID and question, as asked
  // QR, opcode QUERY, AA, RD copied from the query, RA, RCODE 0.
  Write16(out + 2, static_cast<uint16_t>(0x8580U | (flags & 0x0100U)));  // NOLINT(*-arithmetic)
  Write16(out + 6, answer ? 1 : 0);  // ANCOUNT  // NOLINT(*-pointer-arithmetic)
  Write16(out + 8, 0);               // NSCOUNT  // NOLINT(*-pointer-arithmetic)
  Write16(out + 10, 0);              // ARCOUNT  // NOLINT(*-pointer-arithmetic)
  if (answer) {
    uint8_t* a = out + question_end;                               // NOLINT(*-pointer-arithmetic)
    Write16(a, 0xC000U | kHeaderBytes);                            // pointer to the question's name
    Write16(a + 2, kTypeA);                                        // NOLINT(*-pointer-arithmetic)
    Write16(a + 4, kClassIn);                                      // NOLINT(*-pointer-arithmetic)
    Write16(a + 6, static_cast<uint16_t>(kTtlSeconds >> 16U));     // NOLINT(*-arithmetic)
    Write16(a + 8, static_cast<uint16_t>(kTtlSeconds & 0xFFFFU));  // NOLINT(*-arithmetic)
    Write16(a + 10, 4);                                            // NOLINT(*-arithmetic)
    std::memcpy(a + 12, ip.data(), ip.size());                     // NOLINT(*-arithmetic)
  }
  return reply_size;
}

}  // namespace sarathi::core
