// The captive portal's DNS answers (ADR-0004 §2): every name resolves to the device, so a
// phone that joins the setup access point opens its "sign in to network" page. Pure logic:
// builds the reply to one DNS query (RFC 1035).

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace sarathi::core {

constexpr size_t kDnsMaxPacket = 512;  // DNS over UDP

// Writes the reply to `query` into `out` and returns its size, or 0 for anything that isn't a
// single-question standard query (those are ignored). A-record questions get `ip`; other
// types get an empty answer, so phones fall back to IPv4.
[[nodiscard]] size_t BuildCaptiveDnsReply(const uint8_t* query, size_t size,
                                          const std::array<uint8_t, 4>& ip, uint8_t* out,
                                          size_t capacity);

}  // namespace sarathi::core
