#include "ota_manifest.h"

#include "base64url.h"
#include "ota_version.h"

namespace sarathi::core {
namespace {

constexpr std::string_view kAlg = "ES256";
constexpr std::string_view kTyp = "sarathi-manifest+json";
constexpr uint32_t kSchema = 1;

std::string_view AsText(const uint8_t* data, size_t size) {
  return {reinterpret_cast<const char*>(data), size};  // NOLINT(*-reinterpret-cast)
}

// Reads an ASCII string member into a FixedString. kTooLong means it can't be the value we're
// comparing against, which callers map to their own error.
template <size_t N>
JsonGet ReadString(const JsonReader& json, std::string_view name, FixedString<N>* out) {
  std::array<char, N + 1> buffer{};
  size_t length = 0;
  const JsonGet got = json.String(name, buffer.data(), buffer.size(), &length);
  if (got == JsonGet::kOk) {
    out->Assign({buffer.data(), length});
  }
  return got;
}

bool ReadUint32(const JsonReader& json, std::string_view name, uint32_t* out) {
  uint64_t value = 0;
  if (json.Uint(name, &value) != JsonGet::kOk || value > UINT32_MAX) {
    return false;
  }
  *out = static_cast<uint32_t>(value);
  return true;
}

bool ParseSha256Hex(std::string_view hex, Sha256* out) {
  if (hex.size() != 2 * kSha256Bytes) {
    return false;
  }
  for (size_t i = 0; i < kSha256Bytes; ++i) {
    uint32_t byte = 0;
    for (size_t k = 0; k < 2; ++k) {
      const char c = hex[(2 * i) + k];
      uint32_t nibble = 0;
      if (c >= '0' && c <= '9') {
        nibble = static_cast<uint32_t>(c - '0');
      } else if (c >= 'a' && c <= 'f') {
        nibble = static_cast<uint32_t>(c - 'a' + 10);
      } else {
        return false;  // lowercase only
      }
      byte = (byte << 4U) | nibble;
    }
    out->at(i) = static_cast<uint8_t>(byte);
  }
  return true;
}

}  // namespace

const char* OtaErrorCode(OtaError error) {
  switch (error) {
    case OtaError::kNone:
      return "ok";
    case OtaError::kMalformed:
      return "malformed";
    case OtaError::kUnsupportedAlg:
      return "unsupported_alg";
    case OtaError::kUnsupportedType:
      return "unsupported_type";
    case OtaError::kUnknownKid:
      return "unknown_kid";
    case OtaError::kBadSignature:
      return "bad_signature";
    case OtaError::kBadSchema:
      return "bad_schema";
    case OtaError::kWrongBoard:
      return "wrong_board";
    case OtaError::kNotNewer:
      return "not_newer";
    case OtaError::kTooLarge:
      return "too_large";
    case OtaError::kInsecureUrl:
      return "insecure_url";
    case OtaError::kSizeMismatch:
      return "size_mismatch";
    case OtaError::kHashMismatch:
      return "hash_mismatch";
  }
  return "?";
}

OtaError VerifyManifest(std::string_view token, const ManifestContext& context,
                        const SignatureVerifier& verifier, ManifestScratch& scratch,
                        Manifest* out) {
  while (!token.empty() && (token.back() == '\n' || token.back() == '\r')) {
    token.remove_suffix(1);
  }

  // 1. Parse.
  if (token.size() > kMaxManifestBytes) {
    return OtaError::kMalformed;
  }
  for (const char c : token) {
    if (static_cast<uint8_t>(c) >= 0x80) {
      return OtaError::kMalformed;
    }
  }
  const size_t dot1 = token.find('.');
  const size_t dot2 = dot1 == std::string_view::npos ? dot1 : token.find('.', dot1 + 1);
  if (dot2 == std::string_view::npos || token.find('.', dot2 + 1) != std::string_view::npos ||
      dot1 == 0 || dot2 == dot1 + 1) {
    return OtaError::kMalformed;
  }
  size_t header_size = 0;
  size_t payload_size = 0;
  size_t signature_size = 0;
  if (!Base64UrlDecode(token.substr(0, dot1), scratch.header.data(), scratch.header.size(),
                       &header_size) ||
      !scratch.json.Parse(AsText(scratch.header.data(), header_size)) ||
      !Base64UrlDecode(token.substr(dot1 + 1, dot2 - dot1 - 1), scratch.payload.data(),
                       scratch.payload.size(), &payload_size) ||
      !Base64UrlDecode(token.substr(dot2 + 1), scratch.signature.data(), scratch.signature.size(),
                       &signature_size)) {
    return OtaError::kMalformed;
  }

  // 2. Header.
  const JsonReader& header = scratch.json;
  if (header.Has("crit")) {
    return OtaError::kMalformed;
  }
  FixedString<8> alg;
  if (ReadString(header, "alg", &alg) != JsonGet::kOk || alg.View() != kAlg) {
    return OtaError::kUnsupportedAlg;
  }
  FixedString<32> typ;
  if (ReadString(header, "typ", &typ) != JsonGet::kOk || typ.View() != kTyp) {
    return OtaError::kUnsupportedType;
  }
  if (ReadString(header, "kid", &out->kid) != JsonGet::kOk || !verifier.Trusts(out->kid.View())) {
    return OtaError::kUnknownKid;
  }

  // 3. Signature. Nothing in the payload is read before this passes.
  if (signature_size != kEs256SignatureBytes ||
      !verifier.Verify(out->kid.View(), token.substr(0, dot2), scratch.signature.data())) {
    return OtaError::kBadSignature;
  }

  // 4. Payload. Every member must be present with the right type before any value is judged,
  // as in the spec (and tools/ota_manifest.py).
  JsonReader& payload = scratch.json;
  if (!payload.Parse(AsText(scratch.payload.data(), payload_size))) {
    return OtaError::kMalformed;
  }
  uint32_t schema = 0;
  uint64_t min_protocol = 0;
  FixedString<11> version;
  FixedString<64> sha256_hex;
  if (!ReadUint32(payload, "schema", &schema)) {
    return OtaError::kBadSchema;
  }
  const JsonGet board = ReadString(payload, "board", &out->board);  // judged below
  if ((board != JsonGet::kOk && board != JsonGet::kTooLong) ||
      ReadString(payload, "version", &version) != JsonGet::kOk ||
      !ReadUint32(payload, "seq", &out->seq) || payload.Uint("size", &out->size) != JsonGet::kOk ||
      ReadString(payload, "sha256", &sha256_hex) != JsonGet::kOk ||
      payload.Uint("min_protocol", &min_protocol) != JsonGet::kOk ||
      ReadString(payload, "url", &out->url) != JsonGet::kOk) {
    return OtaError::kBadSchema;
  }
  out->version = version;
  // Advisory only, so a value beyond 32 bits saturates instead of failing.
  out->min_protocol = min_protocol > UINT32_MAX ? UINT32_MAX : static_cast<uint32_t>(min_protocol);
  if (schema != kSchema || SeqForVersion(version.View()) != static_cast<int64_t>(out->seq) ||
      !ParseSha256Hex(sha256_hex.View(), &out->sha256) || out->size == 0) {
    return OtaError::kBadSchema;
  }
  if (board != JsonGet::kOk || out->board.View() != context.board) {
    return OtaError::kWrongBoard;
  }
  if (out->seq <= context.running_seq) {
    return OtaError::kNotNewer;
  }
  if (out->size > context.slot_size) {
    return OtaError::kTooLarge;
  }
  if (out->url.View().substr(0, 8) != "https://") {
    return OtaError::kInsecureUrl;
  }
  return OtaError::kNone;
}

bool ImageCheck::Add(size_t bytes) {
  received_ += bytes;
  return received_ <= expected_size_;
}

OtaError ImageCheck::Finish(const Sha256& sha256) const {
  if (received_ != expected_size_) {
    return OtaError::kSizeMismatch;
  }
  return sha256 == expected_sha256_ ? OtaError::kNone : OtaError::kHashMismatch;
}

}  // namespace sarathi::core
