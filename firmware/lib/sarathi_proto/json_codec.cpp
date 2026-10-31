#include "json_codec.h"

#include <array>
#include <string_view>

#include "json_writer.h"

namespace sarathi::proto {
namespace {

using core::JsonGet;
using core::JsonReader;
using core::kRootToken;

// Value names in schema enum order (index = enum value).
constexpr std::array<std::string_view, 4> kRoutingNames = {"google", "mappls", "osm_online",
                                                           "osm_offline"};
constexpr std::array<std::string_view, 3> kSearchNames = {"google", "mappls", "osm"};
constexpr std::array<std::string_view, 3> kEventKindNames = {"fallback", "reroute", "cost_warning"};
constexpr std::array<std::string_view, 5> kReasonNames = {"quota", "offline", "error", "no_route",
                                                          "account"};
constexpr std::array<std::string_view, 3> kNavEndNames = {"arrived", "cancelled", "error"};
constexpr std::array<std::string_view, 3> kCallPhaseNames = {"ringing", "active", "idle"};
constexpr std::array<std::string_view, 4> kMapStyleNames = {"classic", "hud", "racing", "minimal"};
constexpr std::array<std::string_view, 3> kRefuseNames = {"full", "invalid", "epoch"};
constexpr std::array<std::string_view, 2> kCallChoiceNames = {"accept", "reject"};
constexpr std::array<std::string_view, 3> kMediaKeyNames = {"play_pause", "next", "prev"};
constexpr std::array<std::string_view, 7> kPhoneCapNames = {"google",  "mappls", "osm",  "offline",
                                                            "traffic", "calls",  "media"};
constexpr std::array<std::string_view, 3> kNavFlagNames = {"off_route", "rerouting", "arrived"};
constexpr std::array<std::string_view, 2> kMapLayerNames = {"water", "parks"};

constexpr size_t kMaxEnumBytes = 16;  // longer than any enum value name

template <typename E, size_t N>
std::string_view NameOf(const std::array<std::string_view, N>& names, E e) {
  return names.at(static_cast<size_t>(e));
}

// ---- Decoding: each Get* returns false if the value is missing or breaks the schema.

class Fields {
 public:
  explicit Fields(const JsonReader& r) : r_(r) {}

  template <typename T>
  bool Uint(int object, std::string_view name, uint64_t min, uint64_t max, T* out) const {
    return UintAt(r_.Member(object, name), min, max, out);
  }

  template <typename T>
  bool UintAt(int token, uint64_t min, uint64_t max, T* out) const {
    uint64_t v = 0;
    if (r_.UintAt(token, &v) != JsonGet::kOk || v < min || v > max) {
      return false;
    }
    *out = static_cast<T>(v);
    return true;
  }

  bool Int16(int object, std::string_view name, int64_t min, int64_t max, int16_t* out) const {
    int64_t v = 0;
    if (r_.IntAt(r_.Member(object, name), &v) != JsonGet::kOk || v < min || v > max) {
      return false;
    }
    *out = static_cast<int16_t>(v);
    return true;
  }

  bool Bool(int object, std::string_view name, bool* out) const {
    return r_.BoolAt(r_.Member(object, name), out) == JsonGet::kOk;
  }

  // A UTF-8 string of at most N bytes.
  template <size_t N>
  bool Text(int object, std::string_view name, FixedString<N>* out) const {
    std::array<char, N + 1> buffer{};
    size_t length = 0;
    if (r_.Utf8At(r_.Member(object, name), buffer.data(), buffer.size(), &length) != JsonGet::kOk) {
      return false;
    }
    out->Assign(std::string_view(buffer.data(), length));
    return true;
  }

  template <typename E, size_t N>
  bool EnumAt(int token, const std::array<std::string_view, N>& names, E* out) const {
    std::array<char, kMaxEnumBytes + 1> buffer{};
    size_t length = 0;
    if (r_.StringAt(token, buffer.data(), buffer.size(), &length) != JsonGet::kOk) {
      return false;
    }
    const std::string_view value(buffer.data(), length);
    for (size_t i = 0; i < N; ++i) {
      if (names.at(i) == value) {
        *out = static_cast<E>(i);
        return true;
      }
    }
    return false;
  }

  template <typename E, size_t N>
  bool Enum(int object, std::string_view name, const std::array<std::string_view, N>& names,
            E* out) const {
    return EnumAt(r_.Member(object, name), names, out);
  }

  // An array of enum values, at most max_items long, as a bit mask (repeats allowed).
  template <size_t N>
  bool Set(int object, std::string_view name, const std::array<std::string_view, N>& names,
           int max_items, uint16_t* out) const {
    const int array = r_.Member(object, name);
    if (!r_.IsArray(array) || r_.Count(array) > max_items) {
      return false;
    }
    uint16_t bits = 0;
    for (int i = 0; i < r_.Count(array); ++i) {
      uint8_t value = 0;
      if (!EnumAt(r_.Element(array, i), names, &value)) {
        return false;
      }
      bits = static_cast<uint16_t>(bits | (1U << value));
    }
    *out = bits;
    return true;
  }

  [[nodiscard]] bool Has(int object, std::string_view name) const {
    return r_.Member(object, name) != core::kNoToken;
  }

  [[nodiscard]] const JsonReader& Reader() const { return r_; }

 private:
  const JsonReader& r_;
};

constexpr int kRoot = kRootToken;

bool DecodeHello(const Fields& f, HelloPhone* m) {
  const JsonReader& r = f.Reader();
  const int proto = r.Member(kRoot, "proto");
  return r.IsArray(proto) && r.Count(proto) == 2 &&
         f.UintAt(r.Element(proto, 0), 0, 255, &m->proto_major) &&
         f.UintAt(r.Element(proto, 1), 0, 255, &m->proto_minor) && f.Text(kRoot, "app", &m->app) &&
         f.Set(kRoot, "caps", kPhoneCapNames, 16, &m->caps);
}

bool DecodeTime(const Fields& f, Time* m) {
  if (!f.Text(kRoot, "local", &m->local) || m->local.Size() != 19) {
    return false;
  }
  constexpr std::string_view kPattern = "dddd-dd-ddTdd:dd:dd";
  for (size_t i = 0; i < kPattern.size(); ++i) {
    const char c = m->local.View()[i];
    if (kPattern[i] == 'd' ? (c < '0' || c > '9') : c != kPattern[i]) {
      return false;
    }
  }
  return f.Int16(kRoot, "utc_offset_min", -720, 840, &m->utc_offset_min);
}

bool DecodePhoneBatt(const Fields& f, PhoneBatt* m) {
  return f.Uint(kRoot, "level", 0, 100, &m->level) && f.Bool(kRoot, "charging", &m->charging);
}

bool DecodeFrame(const Fields& f, FrameEpoch* m) {
  return f.Uint(kRoot, "epoch", 0, 65535, &m->epoch);
}

bool DecodeBackend(const Fields& f, Backend* m) {
  return f.Enum(kRoot, "routing", kRoutingNames, &m->routing) &&
         f.Enum(kRoot, "search", kSearchNames, &m->search) && f.Text(kRoot, "name", &m->name) &&
         f.Text(kRoot, "attribution", &m->attribution) && f.Bool(kRoot, "traffic", &m->traffic) &&
         f.Bool(kRoot, "two_wheeler", &m->two_wheeler);
}

// The optional fields are checked whenever present; the kind decides which are required.
bool DecodeEvent(const Fields& f, Event* m) {
  if (!f.Enum(kRoot, "kind", kEventKindNames, &m->kind)) {
    return false;
  }
  const bool fallback = m->kind == EventKind::kFallback;
  const bool cost = m->kind == EventKind::kCostWarning;
  const auto field = [&f](std::string_view name, bool required, auto&& get) {
    return f.Has(kRoot, name) ? get() : !required;
  };
  return field("from", fallback, [&] { return f.Enum(kRoot, "from", kRoutingNames, &m->from); }) &&
         field("to", fallback, [&] { return f.Enum(kRoot, "to", kRoutingNames, &m->to); }) &&
         field("reason", fallback,
               [&] { return f.Enum(kRoot, "reason", kReasonNames, &m->reason); }) &&
         field("backend", cost,
               [&] { return f.Enum(kRoot, "backend", kSearchNames, &m->backend); }) &&
         field("percent", cost, [&] { return f.Uint(kRoot, "percent", 0, 100, &m->percent); });
}

bool DecodeNav(const Fields& f, Nav* m) {
  return f.Text(kRoot, "m", &m->m) && f.Uint(kRoot, "exit", 0, 15, &m->exit) &&
         f.Uint(kRoot, "dist_m", 0, 1000000, &m->dist_m) && f.Text(kRoot, "street", &m->street) &&
         f.Uint(kRoot, "rem_s", 0, 604800, &m->rem_s) &&
         f.Uint(kRoot, "rem_m", 0, 10000000, &m->rem_m) &&
         f.Set(kRoot, "flags", kNavFlagNames, 3, &m->flags);
}

bool DecodeNavEnd(const Fields& f, NavEnd* m) {
  return f.Enum(kRoot, "reason", kNavEndNames, &m->reason);
}

bool DecodeCall(const Fields& f, Call* m) {
  return f.Enum(kRoot, "phase", kCallPhaseNames, &m->phase) && f.Text(kRoot, "caller", &m->caller);
}

bool DecodeMedia(const Fields& f, Media* m) {
  return f.Bool(kRoot, "avail", &m->avail) && f.Bool(kRoot, "playing", &m->playing) &&
         f.Text(kRoot, "title", &m->title) && f.Text(kRoot, "artist", &m->artist) &&
         f.Uint(kRoot, "pos_s", 0, 86400, &m->pos_s) && f.Uint(kRoot, "dur_s", 0, 86400, &m->dur_s);
}

template <typename T>
bool DecodeInto(const Fields& f, PhoneMessage* out, bool (*decode)(const Fields&, T*)) {
  T message{};
  if (!decode(f, &message)) {
    return false;
  }
  *out = message;
  return true;
}

// ---- Encoding.

// Starts an object with "t"; Done closes it and applies the size limits.
JsonWriter Begin(char* out, size_t capacity, MsgType type) {
  JsonWriter w(out, capacity < kMaxJson ? capacity : kMaxJson);
  w.BeginObject();
  w.Key("t");
  w.String(MsgTypeName(static_cast<uint8_t>(type)));
  return w;
}

size_t Done(JsonWriter& w) {
  w.EndObject();
  return w.Finish();
}

template <size_t N>
void WriteSet(JsonWriter& w, std::string_view key, const std::array<std::string_view, N>& names,
              uint16_t bits) {
  w.Key(key);
  w.BeginArray();
  for (size_t i = 0; i < N; ++i) {
    if ((bits & (1U << i)) != 0) {
      w.String(names.at(i));
    }
  }
  w.EndArray();
}

void WriteProto(JsonWriter& w, uint8_t major, uint8_t minor) {
  w.Key("proto");
  w.BeginArray();
  w.Uint(major);
  w.Uint(minor);
  w.EndArray();
}

void WriteKey(JsonWriter& w, const ChunkKey& k) {
  w.Uint(k.level);
  w.Uint(k.tile_x);
  w.Uint(k.tile_y);
  w.Uint(k.data_version);
}

}  // namespace

const char* JsonErrorName(JsonError error) {
  switch (error) {
    case JsonError::kNone:
      return "none";
    case JsonError::kUnknownType:
      return "unknown_type";
    case JsonError::kTooLong:
      return "too_long";
    case JsonError::kNotJson:
      return "not_json";
    case JsonError::kTypeMismatch:
      return "type_mismatch";
    case JsonError::kInvalid:
      return "invalid";
  }
  return "?";
}

JsonError PhoneJsonDecoder::Decode(uint8_t type, const uint8_t* data, size_t size,
                                   PhoneMessage* out) {
  if (type < static_cast<uint8_t>(MsgType::kHello) ||
      type > static_cast<uint8_t>(MsgType::kMedia)) {
    return JsonError::kUnknownType;
  }
  if (size > kMaxJson) {
    return JsonError::kTooLong;
  }
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast): bytes viewed as text
  const std::string_view text(reinterpret_cast<const char*>(data), size);
  if (!JsonReader::IsValidJson(text)) {
    return JsonError::kNotJson;
  }
  const size_t start = text.find_first_not_of(" \t\r\n");
  if (start == std::string_view::npos || text[start] != '{') {
    return JsonError::kTypeMismatch;
  }
  if (!reader_.Parse(text)) {
    return JsonError::kInvalid;  // a repeated name, or more tokens than the reader holds
  }
  const std::string_view name = MsgTypeName(type);
  std::array<char, kMaxEnumBytes + 1> t{};
  size_t length = 0;
  if (reader_.String("t", t.data(), t.size(), &length) != JsonGet::kOk ||
      std::string_view(t.data(), length) != name) {
    return JsonError::kTypeMismatch;
  }
  const Fields f(reader_);
  bool ok = false;
  switch (static_cast<MsgType>(type)) {
    case MsgType::kHello:
      ok = DecodeInto(f, out, DecodeHello);
      break;
    case MsgType::kTime:
      ok = DecodeInto(f, out, DecodeTime);
      break;
    case MsgType::kPhoneBatt:
      ok = DecodeInto(f, out, DecodePhoneBatt);
      break;
    case MsgType::kFrame:
      ok = DecodeInto(f, out, DecodeFrame);
      break;
    case MsgType::kBackend:
      ok = DecodeInto(f, out, DecodeBackend);
      break;
    case MsgType::kEvent:
      ok = DecodeInto(f, out, DecodeEvent);
      break;
    case MsgType::kNav:
      ok = DecodeInto(f, out, DecodeNav);
      break;
    case MsgType::kNavEnd:
      ok = DecodeInto(f, out, DecodeNavEnd);
      break;
    case MsgType::kCall:
      ok = DecodeInto(f, out, DecodeCall);
      break;
    case MsgType::kMedia:
      ok = DecodeInto(f, out, DecodeMedia);
      break;
    default:
      return JsonError::kUnknownType;
  }
  return ok ? JsonError::kNone : JsonError::kInvalid;
}

size_t EncodeJson(const HelloPhone& m, char* out, size_t capacity) {
  JsonWriter w = Begin(out, capacity, MsgType::kHello);
  WriteProto(w, m.proto_major, m.proto_minor);
  w.Key("app");
  w.String(m.app.View());
  WriteSet(w, "caps", kPhoneCapNames, m.caps);
  return Done(w);
}

size_t EncodeJson(const Time& m, char* out, size_t capacity) {
  JsonWriter w = Begin(out, capacity, MsgType::kTime);
  w.Key("local");
  w.String(m.local.View());
  w.Key("utc_offset_min");
  w.Int(m.utc_offset_min);
  return Done(w);
}

size_t EncodeJson(const PhoneBatt& m, char* out, size_t capacity) {
  JsonWriter w = Begin(out, capacity, MsgType::kPhoneBatt);
  w.Key("level");
  w.Uint(m.level);
  w.Key("charging");
  w.Bool(m.charging);
  return Done(w);
}

size_t EncodeJson(const FrameEpoch& m, char* out, size_t capacity) {
  JsonWriter w = Begin(out, capacity, MsgType::kFrame);
  w.Key("epoch");
  w.Uint(m.epoch);
  return Done(w);
}

size_t EncodeJson(const Backend& m, char* out, size_t capacity) {
  JsonWriter w = Begin(out, capacity, MsgType::kBackend);
  w.Key("routing");
  w.String(NameOf(kRoutingNames, m.routing));
  w.Key("search");
  w.String(NameOf(kSearchNames, m.search));
  w.Key("name");
  w.String(m.name.View());
  w.Key("attribution");
  w.String(m.attribution.View());
  w.Key("traffic");
  w.Bool(m.traffic);
  w.Key("two_wheeler");
  w.Bool(m.two_wheeler);
  return Done(w);
}

size_t EncodeJson(const Event& m, char* out, size_t capacity) {
  JsonWriter w = Begin(out, capacity, MsgType::kEvent);
  w.Key("kind");
  w.String(NameOf(kEventKindNames, m.kind));
  if (m.kind == EventKind::kFallback) {
    w.Key("from");
    w.String(NameOf(kRoutingNames, m.from));
    w.Key("to");
    w.String(NameOf(kRoutingNames, m.to));
    w.Key("reason");
    w.String(NameOf(kReasonNames, m.reason));
  } else if (m.kind == EventKind::kCostWarning) {
    w.Key("backend");
    w.String(NameOf(kSearchNames, m.backend));
    w.Key("percent");
    w.Uint(m.percent);
  }
  return Done(w);
}

size_t EncodeJson(const Nav& m, char* out, size_t capacity) {
  JsonWriter w = Begin(out, capacity, MsgType::kNav);
  w.Key("m");
  w.String(m.m.View());
  w.Key("exit");
  w.Uint(m.exit);
  w.Key("dist_m");
  w.Uint(m.dist_m);
  w.Key("street");
  w.String(m.street.View());
  w.Key("rem_s");
  w.Uint(m.rem_s);
  w.Key("rem_m");
  w.Uint(m.rem_m);
  WriteSet(w, "flags", kNavFlagNames, m.flags);
  return Done(w);
}

size_t EncodeJson(const NavEnd& m, char* out, size_t capacity) {
  JsonWriter w = Begin(out, capacity, MsgType::kNavEnd);
  w.Key("reason");
  w.String(NameOf(kNavEndNames, m.reason));
  return Done(w);
}

size_t EncodeJson(const Call& m, char* out, size_t capacity) {
  JsonWriter w = Begin(out, capacity, MsgType::kCall);
  w.Key("phase");
  w.String(NameOf(kCallPhaseNames, m.phase));
  w.Key("caller");
  w.String(m.caller.View());
  return Done(w);
}

size_t EncodeJson(const Media& m, char* out, size_t capacity) {
  JsonWriter w = Begin(out, capacity, MsgType::kMedia);
  w.Key("avail");
  w.Bool(m.avail);
  w.Key("playing");
  w.Bool(m.playing);
  w.Key("title");
  w.String(m.title.View());
  w.Key("artist");
  w.String(m.artist.View());
  w.Key("pos_s");
  w.Uint(m.pos_s);
  w.Key("dur_s");
  w.Uint(m.dur_s);
  return Done(w);
}

size_t EncodeJson(const HelloDevice& m, char* out, size_t capacity) {
  if (m.style_count > kMaxStyles) {
    return 0;
  }
  JsonWriter w = Begin(out, capacity, MsgType::kHello);
  WriteProto(w, m.proto_major, m.proto_minor);
  w.Key("fw");
  w.String(m.fw.View());
  w.Key("seq");
  w.Uint(m.seq);
  w.Key("caps");
  w.BeginObject();
  w.Key("styles");
  w.BeginArray();
  for (size_t i = 0; i < m.style_count; ++i) {
    w.String(m.styles.at(i).View());
  }
  w.EndArray();
  w.Key("chunk_kb");
  w.Uint(m.chunk_kb);
  w.Key("max_msg");
  w.Uint(m.max_msg);
  w.EndObject();
  w.Key("route");
  if (m.has_route) {
    w.BeginObject();
    w.Key("id");
    w.Uint(m.route_id);
    w.Key("v");
    w.Uint(m.route_v);
    w.EndObject();
  } else {
    w.Null();
  }
  w.Key("epoch");
  if (m.has_epoch) {
    w.Uint(m.epoch);
  } else {
    w.Null();
  }
  return Done(w);
}

size_t EncodeJson(const ChunkAck& m, char* out, size_t capacity) {
  if (m.stored_count > kMaxAckEntries || m.refused_count > kMaxAckEntries ||
      (m.stored_count > 0 && m.stored == nullptr) ||
      (m.refused_count > 0 && m.refused == nullptr)) {
    return 0;
  }
  JsonWriter w = Begin(out, capacity, MsgType::kChunkAck);
  w.Key("stored");
  w.BeginArray();
  for (size_t i = 0; i < m.stored_count; ++i) {
    w.BeginArray();
    WriteKey(w, m.stored[i]);  // NOLINT(cppcoreguidelines-pro-bounds-pointer-arithmetic)
    w.EndArray();
  }
  w.EndArray();
  w.Key("refused");
  w.BeginArray();
  for (size_t i = 0; i < m.refused_count; ++i) {
    const RefusedChunk& r =
        m.refused[i];  // NOLINT(cppcoreguidelines-pro-bounds-pointer-arithmetic)
    w.BeginArray();
    WriteKey(w, r.key);
    w.String(NameOf(kRefuseNames, r.reason));
    w.EndArray();
  }
  w.EndArray();
  return Done(w);
}

size_t EncodeJson(const Flow& m, char* out, size_t capacity) {
  JsonWriter w = Begin(out, capacity, MsgType::kFlow);
  w.Key("window");
  w.Uint(m.window);
  w.Key("consumed");
  w.Uint(m.consumed);
  return Done(w);
}

size_t EncodeJson(const Style& m, char* out, size_t capacity) {
  JsonWriter w = Begin(out, capacity, MsgType::kStyle);
  w.Key("map");
  w.String(NameOf(kMapStyleNames, m.map));
  WriteSet(w, "layers", kMapLayerNames, m.layers);
  return Done(w);
}

size_t EncodeJson(const CallAction& m, char* out, size_t capacity) {
  JsonWriter w = Begin(out, capacity, MsgType::kCallAction);
  w.Key("action");
  w.String(NameOf(kCallChoiceNames, m.action));
  return Done(w);
}

size_t EncodeJson(const MediaCmd& m, char* out, size_t capacity) {
  JsonWriter w = Begin(out, capacity, MsgType::kMediaCmd);
  w.Key("cmd");
  w.String(NameOf(kMediaKeyNames, m.cmd));
  return Done(w);
}

}  // namespace sarathi::proto
