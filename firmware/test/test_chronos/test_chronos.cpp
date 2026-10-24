// Chronos receiver and bridge (sarathi_link, ADR-0005), against recorded and synthetic packets.

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include <unity.h>

#include "app_model.h"
#include "chronos_bridge.h"
#include "chronos_packets.h"
#include "chronos_recorded.h"

using sarathi::core::AppModel;
using sarathi::core::CallPhase;
using sarathi::core::Command;
using sarathi::core::CommandType;
using sarathi::core::kManeuverIconBytes;
using sarathi::link::ChronosAction;
using sarathi::link::ChronosBridge;
using sarathi::link::ChronosDrop;
using sarathi::link::ChronosKind;
using sarathi::link::ChronosMessage;
using sarathi::link::ChronosReceiver;
using sarathi::link::kChronosIconChunkBytes;
using sarathi::link::kChronosMaxPacket;

// Unity calls these by name, so they need external linkage and Unity's spelling.
void setUp() {}     // NOLINT(misc-use-internal-linkage,readability-identifier-naming)
void tearDown() {}  // NOLINT(misc-use-internal-linkage,readability-identifier-naming)

namespace {

using Bytes = std::vector<uint8_t>;

constexpr uint32_t kIconCrc = 0x12345678;

template <size_t N>
Bytes Of(const uint8_t (&a)[N]) {  // NOLINT(*-avoid-c-arrays)
  return Bytes(a, a + N);
}

// A packet: 0xAB, length, flag, command, then the body; the length covers everything after
// the three header bytes.
Bytes Packet(uint8_t flag, uint8_t command, const Bytes& body) {
  const size_t length = 2 + body.size();
  Bytes p = {0xAB, static_cast<uint8_t>(length >> 8U), static_cast<uint8_t>(length & 0xFFU), flag,
             command};
  p.insert(p.end(), body.begin(), body.end());
  return p;
}

void AppendText(Bytes& b, std::string_view text) {
  b.insert(b.end(), text.begin(), text.end());
  b.push_back(0);
}

void AppendU32(Bytes& b, uint32_t v) {
  for (int shift = 24; shift >= 0; shift -= 8) {
    b.push_back(static_cast<uint8_t>(v >> static_cast<unsigned>(shift)));
  }
}

// Navigation data (0xEF, 0x80), as the library parses it.
Bytes SynNav(std::string_view title, std::string_view duration, std::string_view distance,
             std::string_view eta, std::string_view directions, bool has_icon = true,
             uint32_t crc = kIconCrc) {
  Bytes body = {0x80, static_cast<uint8_t>(has_icon ? 1 : 0), 1};
  AppendU32(body, crc);
  AppendText(body, title);
  AppendText(body, duration);
  AppendText(body, distance);
  AppendText(body, eta);
  AppendText(body, directions);
  AppendText(body, "");  // speed: OsmAnd only
  return Packet(0xFE, 0xEF, body);
}

Bytes SynNavInactive() { return Packet(0xFE, 0xEF, {0x00}); }

// One third of the icon (0xEE): chunk index, CRC, 96 bytes.
Bytes SynIconChunk(uint8_t chunk, uint32_t crc, uint8_t fill) {
  Bytes body = {0x80, chunk};
  AppendU32(body, crc);
  body.insert(body.end(), kChronosIconChunkBytes, fill);
  return Packet(0xFE, 0xEE, body);
}

// Splits a packet the way the app does over a small MTU: 20 bytes, then a sequence number
// and up to 19 bytes per write.
std::vector<Bytes> Split(const Bytes& p) {
  std::vector<Bytes> writes;
  writes.emplace_back(p.begin(), p.begin() + 20);
  uint8_t seq = 0;
  for (size_t at = 20; at < p.size(); at += 19) {
    Bytes w = {seq++};
    const size_t end = at + 19 < p.size() ? at + 19 : p.size();
    w.insert(w.end(), p.begin() + static_cast<std::ptrdiff_t>(at),
             p.begin() + static_cast<std::ptrdiff_t>(end));
    writes.push_back(w);
  }
  return writes;
}

struct Rig {
  ChronosReceiver receiver;
  ChronosBridge bridge;
  AppModel model;
  uint32_t now_ms = 1000;

  Rig() {
    bridge.Begin(model);
    bridge.OnConnection(true, model);
  }

  // Feeds one write; true if it completed a packet we use.
  bool Write(const Bytes& w) {
    ChronosMessage m;
    if (!receiver.Feed(w.data(), w.size(), m)) {
      return false;
    }
    bridge.OnMessage(m, now_ms, model);
    return true;
  }
};

// ---- Recorded packets ----

void TestRecordedTimeSetsTheClock() {
  Rig r;
  TEST_ASSERT_TRUE(r.Write(Of(kRecTime)));
  TEST_ASSERT_TRUE(r.model.State().clock.valid);
  TEST_ASSERT_EQUAL_UINT16((19 * 60) + 26, r.model.State().clock.minutes_of_day);
  r.now_ms += 30U * 1000U;  // 19:26:30 + 30 s
  r.bridge.Tick(r.now_ms, r.model);
  TEST_ASSERT_EQUAL_UINT16((19 * 60) + 27, r.model.State().clock.minutes_of_day);
}

void TestRecordedBattery() {
  Rig r;
  TEST_ASSERT_TRUE(r.Write(Of(kRecBattery)));
  TEST_ASSERT_TRUE(r.model.State().phone.battery_valid);
  TEST_ASSERT_EQUAL_UINT8(77, r.model.State().phone.battery_percent);
  TEST_ASSERT_FALSE(r.model.State().phone.charging);
  TEST_ASSERT_FALSE(r.Write(Of(kRecBatteryFf)));  // the 0xFF variant isn't used
  TEST_ASSERT_EQUAL_UINT32(0, r.receiver.Dropped());
}

void TestRecordedCallRingsThenStops() {
  Rig r;
  TEST_ASSERT_TRUE(r.Write(Of(kRecRingerStart)));
  TEST_ASSERT_EQUAL(CallPhase::kRinging, r.model.State().call.phase);
  TEST_ASSERT_EQUAL_STRING("Contact A", r.model.State().call.caller.CStr());
  TEST_ASSERT_TRUE(r.Write(Of(kRecRingerStop)));
  TEST_ASSERT_EQUAL(CallPhase::kIdle, r.model.State().call.phase);
}

void TestRecordedMusicWithoutAPlayer() {
  Rig r;
  TEST_ASSERT_TRUE(r.Write(Of(kRecMusicState)));
  TEST_ASSERT_TRUE(r.Write(Of(kRecMusicArtist)));
  TEST_ASSERT_FALSE(r.model.State().media.available);  // empty package name: no media session
  TEST_ASSERT_EQUAL_STRING("Chronos", r.model.State().media.artist.CStr());
  TEST_ASSERT_FALSE(r.model.MediaPlayPause());
}

void TestRecordedCutShortPacketIsDropped() {
  Rig r;
  TEST_ASSERT_FALSE(r.Write(Of(kRecMusicTitleCut)));  // waits for the rest...
  TEST_ASSERT_TRUE(r.Write(Of(kRecMusicArtist)));     // ...which never comes
  TEST_ASSERT_EQUAL_UINT32(1, r.receiver.Dropped());
  const auto& drop = r.receiver.LastDrop();
  TEST_ASSERT_EQUAL(ChronosDrop::kCutShort, drop.reason);
  TEST_ASSERT_EQUAL_HEX8(0x9D, drop.command);
  TEST_ASSERT_EQUAL_UINT16(22, drop.expected);
  TEST_ASSERT_EQUAL_UINT16(20, drop.received);
  TEST_ASSERT_TRUE(r.model.State().media.title.Empty());
  TEST_ASSERT_EQUAL_STRING("Chronos", r.model.State().media.artist.CStr());
}

// ---- Music with a player ----

void TestMusicWithAPlayer() {
  Rig r;
  Bytes state = {0x80, 0x01, 0, 0, 0, 0xFF, 0xFF, 0xFF};
  AppendText(state, "Spotify");
  AppendText(state, "com.spotify.music");
  TEST_ASSERT_TRUE(r.Write(Packet(0xFE, 0x9D, state)));
  Bytes title = {0x81, 5};
  AppendText(title, "Kesariya");
  TEST_ASSERT_TRUE(r.Write(Packet(0xFE, 0x9D, title)));
  const auto& media = r.model.State().media;
  TEST_ASSERT_TRUE(media.available);
  TEST_ASSERT_TRUE(media.playing);
  TEST_ASSERT_EQUAL_STRING("Kesariya", media.title.CStr());
  TEST_ASSERT_TRUE(r.model.MediaNext());
  Command c;
  TEST_ASSERT_TRUE(r.model.TakeCommand(c));
  TEST_ASSERT_EQUAL(ChronosAction::kMusicNext, r.bridge.OnCommand(c, r.now_ms, r.model));
}

// ---- Navigation (synthetic) ----

void TestNavInOneWrite() {
  Rig r;
  TEST_ASSERT_TRUE(
      r.Write(SynNav("200 m", "12 min", "4.5 km", "19:40", "Turn left onto Station Road")));
  const auto& nav = r.model.State().nav;
  TEST_ASSERT_TRUE(nav.active);
  TEST_ASSERT_EQUAL_STRING("200 m", nav.distance_text.CStr());
  TEST_ASSERT_EQUAL_STRING("Turn left onto Station Road", nav.street.CStr());
  TEST_ASSERT_EQUAL_STRING("12 min", nav.remaining_text.CStr());
  TEST_ASSERT_FALSE(nav.icon.valid);           // no icon chunks yet
  TEST_ASSERT_TRUE(r.model.SettingsLocked());  // no speed source: locked while navigating
  TEST_ASSERT_TRUE(r.Write(SynNavInactive()));
  TEST_ASSERT_FALSE(r.model.State().nav.active);
  TEST_ASSERT_FALSE(r.model.SettingsLocked());
}

void TestNavSplitOverSmallMtu() {
  Rig r;
  const auto writes =
      Split(SynNav("1.2 km", "25 min", "18 km", "20:05", "Continue on Mahatma Gandhi Road"));
  TEST_ASSERT_TRUE(writes.size() > 3);
  for (size_t i = 0; i + 1 < writes.size(); ++i) {
    TEST_ASSERT_FALSE(r.Write(writes[i]));
  }
  TEST_ASSERT_TRUE(r.Write(writes.back()));
  TEST_ASSERT_EQUAL_STRING("1.2 km", r.model.State().nav.distance_text.CStr());
  TEST_ASSERT_EQUAL_STRING("Continue on Mahatma Gandhi Road", r.model.State().nav.street.CStr());
  TEST_ASSERT_EQUAL_UINT32(0, r.receiver.Dropped());
}

void TestGeneralInfoIsNotNavigation() {
  Rig r;
  Bytes body = {0x80, 0, 0};  // is_navigation = 0
  AppendU32(body, 0);
  AppendText(body, "Google Maps");
  TEST_ASSERT_TRUE(r.Write(Packet(0xFE, 0xEF, body)));
  TEST_ASSERT_FALSE(r.model.State().nav.active);
}

void TestIconShowsWhenAllChunksMatch() {
  Rig r;
  r.Write(SynNav("200 m", "12 min", "4.5 km", "19:40", "Turn left"));
  r.Write(SynIconChunk(0, kIconCrc, 0x11));
  r.Write(SynIconChunk(1, kIconCrc, 0x22));
  TEST_ASSERT_FALSE(r.model.State().nav.icon.valid);
  r.Write(SynIconChunk(2, kIconCrc, 0x33));
  const auto& icon = r.model.State().nav.icon;
  TEST_ASSERT_TRUE(icon.valid);
  TEST_ASSERT_EQUAL_HEX32(kIconCrc, icon.id);
  TEST_ASSERT_EQUAL_HEX8(0x11, icon.bits[0]);
  TEST_ASSERT_EQUAL_HEX8(0x22, icon.bits[kChronosIconChunkBytes]);
  TEST_ASSERT_EQUAL_HEX8(0x33, icon.bits[kManeuverIconBytes - 1]);
}

void TestIconClearsWhenTheManeuverChanges() {
  Rig r;
  r.Write(SynNav("200 m", "12 min", "4.5 km", "19:40", "Turn left"));
  for (uint8_t i = 0; i < 3; ++i) {
    r.Write(SynIconChunk(i, kIconCrc, 0xFF));
  }
  TEST_ASSERT_TRUE(r.model.State().nav.icon.valid);
  // The next maneuver: new text and a new icon CRC before its chunks arrive. The old arrow
  // must not stay up next to the new distance.
  r.Write(SynNav("1 km", "10 min", "4 km", "19:40", "Keep right", true, 0xCAFEF00D));
  TEST_ASSERT_FALSE(r.model.State().nav.icon.valid);
  // Chunks of a picture the status doesn't name are ignored.
  for (uint8_t i = 0; i < 3; ++i) {
    r.Write(SynIconChunk(i, kIconCrc, 0xFF));
  }
  TEST_ASSERT_FALSE(r.model.State().nav.icon.valid);
}

// ---- Connection ----

void TestDisconnectClearsLiveData() {
  Rig r;
  r.Write(SynNav("200 m", "12 min", "4.5 km", "19:40", "Turn left"));
  r.Write(Of(kRecRingerStart));
  r.bridge.OnConnection(false, r.model);
  TEST_ASSERT_FALSE(r.model.State().phone.connected);
  TEST_ASSERT_FALSE(r.model.State().nav.active);
  TEST_ASSERT_EQUAL(CallPhase::kIdle, r.model.State().call.phase);
  TEST_ASSERT_FALSE(r.model.State().media.available);
}

void TestDismissHidesTheCallLocally() {
  Rig r;
  r.Write(Of(kRecRingerStart));
  TEST_ASSERT_FALSE(r.model.AcceptCall());
  TEST_ASSERT_FALSE(r.model.RejectCall());
  TEST_ASSERT_TRUE(r.model.DismissCall());
  Command c;
  TEST_ASSERT_TRUE(r.model.TakeCommand(c));
  TEST_ASSERT_EQUAL(CommandType::kCallDismiss, c.type);
  TEST_ASSERT_EQUAL(ChronosAction::kNone, r.bridge.OnCommand(c, r.now_ms, r.model));
  TEST_ASSERT_EQUAL(CallPhase::kIdle, r.model.State().call.phase);
}

// ---- Malformed input: what the library would have written out of bounds ----

void TestContinuationFarPastTheBuffer() {
  Rig r;
  const auto writes = Split(SynNav("200 m", "12 min", "4.5 km", "19:40", "Turn left"));
  r.Write(writes[0]);
  Bytes evil(20, 0x41);
  evil[0] = 200;  // the library writes at 20 + 200 * 19 = 3820, past its 512-byte buffer
  TEST_ASSERT_FALSE(r.Write(evil));
  TEST_ASSERT_EQUAL_UINT32(1, r.receiver.Dropped());
  TEST_ASSERT_EQUAL(ChronosDrop::kOutOfOrder, r.receiver.LastDrop().reason);
  TEST_ASSERT_FALSE(r.model.State().nav.active);
}

void TestOutOfOrderContinuationIsDropped() {
  Rig r;
  const auto writes = Split(SynNav("200 m", "12 min", "4.5 km", "19:40", "Turn left onto X"));
  r.Write(writes[0]);
  TEST_ASSERT_FALSE(r.Write(writes[2]));  // skips sequence 0
  TEST_ASSERT_FALSE(r.Write(writes[1]));  // nothing in progress any more
  TEST_ASSERT_EQUAL_UINT32(2, r.receiver.Dropped());
}

void TestStrayContinuationIsDropped() {
  Rig r;
  const Bytes w = {0x00, 0x41, 0x42};
  TEST_ASSERT_FALSE(r.Write(w));
  TEST_ASSERT_EQUAL_UINT32(1, r.receiver.Dropped());
}

void TestOversizedLengthsAreRejected() {
  Rig r;
  Bytes huge = {0xAB, 0xFF, 0xFF, 0xFE, 0xEF, 0x80};  // declares 65,538 bytes
  huge.resize(20, 0);
  TEST_ASSERT_FALSE(r.Write(huge));
  Bytes big = Packet(0xFE, 0xEF, Bytes(kChronosMaxPacket, 0x41));  // 517 bytes, one write
  TEST_ASSERT_FALSE(r.Write(big));
  TEST_ASSERT_EQUAL_UINT32(2, r.receiver.Dropped());
}

void TestFirstWriteMustBeTwentyBytesWhenSplit() {
  Rig r;
  Bytes p = SynNav("200 m", "12 min", "4.5 km", "19:40", "Turn left");
  p.resize(30);  // a 30-byte first write: continuation offsets wouldn't line up
  TEST_ASSERT_FALSE(r.Write(p));
  TEST_ASSERT_EQUAL_UINT32(1, r.receiver.Dropped());
}

void TestIconChunkIndexOutOfRange() {
  Rig r;
  r.Write(SynNav("200 m", "12 min", "4.5 km", "19:40", "Turn left"));
  TEST_ASSERT_FALSE(r.Write(SynIconChunk(5, kIconCrc, 0xFF)));  // the library writes past
  TEST_ASSERT_FALSE(r.model.State().nav.icon.valid);
}

void TestCommandsWeDontUseAreIgnored() {
  Rig r;
  Bytes qr = {0x80, 200};  // QR link at index 200: the library indexes a 9-entry array
  AppendText(qr, "https://example.com");
  TEST_ASSERT_FALSE(r.Write(Packet(0xFF, 0xA8, qr)));
  Bytes contact = {255};  // contact 255 of a 255-entry array
  AppendText(contact, "Someone");
  TEST_ASSERT_FALSE(r.Write(Packet(0xFE, 0xA2, contact)));
  Bytes notification = {0x80, 0x0A, 0x02};  // a WhatsApp notification: never shown
  AppendText(notification, "Hi");
  TEST_ASSERT_FALSE(r.Write(Packet(0xFF, 0x72, notification)));
  TEST_ASSERT_EQUAL_UINT32(0, r.receiver.Dropped());
  TEST_ASSERT_EQUAL(CallPhase::kIdle, r.model.State().call.phase);
}

void TestInvalidTimeIsRejected() {
  Rig r;
  Bytes t = Of(kRecTime);
  t[9] = 13;  // month 13
  TEST_ASSERT_FALSE(r.Write(t));
  TEST_ASSERT_FALSE(r.model.State().clock.valid);
}

void TestTextIsMadeSafe() {
  Rig r;
  Bytes ring = {0x80, 0x01, 0x02};
  const std::string caller =
      std::string("Ana\xC3\xAFs\x01\xFF") + "\xE0\xA4\x85";  // ï, ctrl, bad, अ
  ring.insert(ring.end(), caller.begin(), caller.end());
  TEST_ASSERT_TRUE(r.Write(Packet(0xFF, 0x72, ring)));
  TEST_ASSERT_EQUAL_STRING("Ana\xC3\xAFs ?\xE0\xA4\x85", r.model.State().call.caller.CStr());
}

void TestLongTextIsTruncatedOnACharacter() {
  Rig r;
  std::string name;
  for (int i = 0; i < 20; ++i) {
    name += "\xE0\xA4\x85";  // 3-byte characters: 60 bytes
  }
  Bytes ring = {0x80, 0x01, 0x02};
  ring.insert(ring.end(), name.begin(), name.end());
  r.Write(Packet(0xFF, 0x72, ring));
  TEST_ASSERT_EQUAL_UINT(30, r.model.State().call.caller.Size());  // 10 whole characters of 32
}

}  // namespace

int main() {
  UNITY_BEGIN();
  RUN_TEST(TestRecordedTimeSetsTheClock);
  RUN_TEST(TestRecordedBattery);
  RUN_TEST(TestRecordedCallRingsThenStops);
  RUN_TEST(TestRecordedMusicWithoutAPlayer);
  RUN_TEST(TestRecordedCutShortPacketIsDropped);
  RUN_TEST(TestMusicWithAPlayer);
  RUN_TEST(TestNavInOneWrite);
  RUN_TEST(TestNavSplitOverSmallMtu);
  RUN_TEST(TestGeneralInfoIsNotNavigation);
  RUN_TEST(TestIconShowsWhenAllChunksMatch);
  RUN_TEST(TestIconClearsWhenTheManeuverChanges);
  RUN_TEST(TestDisconnectClearsLiveData);
  RUN_TEST(TestDismissHidesTheCallLocally);
  RUN_TEST(TestContinuationFarPastTheBuffer);
  RUN_TEST(TestOutOfOrderContinuationIsDropped);
  RUN_TEST(TestStrayContinuationIsDropped);
  RUN_TEST(TestOversizedLengthsAreRejected);
  RUN_TEST(TestFirstWriteMustBeTwentyBytesWhenSplit);
  RUN_TEST(TestIconChunkIndexOutOfRange);
  RUN_TEST(TestCommandsWeDontUseAreIgnored);
  RUN_TEST(TestInvalidTimeIsRejected);
  RUN_TEST(TestTextIsMadeSafe);
  RUN_TEST(TestLongTextIsTruncatedOnACharacter);
  return UNITY_END();
}
