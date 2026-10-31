// Our phone link's bridge (sarathi_link/protocol_bridge, ADR-0007): frames in, app state and
// replies out. Messages come from the golden vectors where one fits.

#include <cstdint>
#include <string>
#include <vector>

#include <unity.h>

#include "../protocol_vectors.h"
#include "app_model.h"
#include "binary_codec.h"
#include "frame.h"
#include "json_codec.h"
#include "protocol_bridge.h"

using sarathi::core::AppModel;
using sarathi::core::CallPhase;
using sarathi::core::Command;
using sarathi::core::CommandType;
using sarathi::core::LinkStatus;
using sarathi::core::ManeuverType;
using sarathi::link::BridgeStorage;
using sarathi::link::kFlowWindow;
using sarathi::link::kNavStaleMs;
using sarathi::link::LinkAction;
using sarathi::link::LinkEvent;
using sarathi::link::Outbox;
using sarathi::link::ProtocolBridge;
namespace proto = sarathi::proto;

// Unity calls these by name, so they need external linkage and Unity's spelling.
void setUp() {}     // NOLINT(misc-use-internal-linkage,readability-identifier-naming)
void tearDown() {}  // NOLINT(misc-use-internal-linkage,readability-identifier-naming)

namespace {

struct Sent {
  uint8_t type;
  std::string text;
};

class RecordingOutbox final : public Outbox {
 public:
  bool Send(uint8_t type, const uint8_t* payload, size_t size) override {
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
    sent.push_back({type, std::string(reinterpret_cast<const char*>(payload), size)});
    return true;
  }
  std::vector<Sent> sent;
};

std::vector<uint8_t> g_reassembly(proto::kMaxMessage);
std::vector<proto::Point> g_points(proto::kMaxRoutePoints);
std::vector<sarathi::core::MetricPoint> g_tracked(proto::kMaxRoutePoints);
std::vector<float> g_cumulative(proto::kMaxRoutePoints);

// A bridge on a secured link, before the phone's hello.
struct Fixture {
  Fixture()
      : bridge(BridgeStorage{g_reassembly.data(),
                             g_reassembly.size(),
                             g_points.data(),
                             g_points.size(),
                             {g_tracked.data(), g_cumulative.data(), g_tracked.size()}}) {
    bridge.Begin(model);
    bridge.OnLinkEvent(LinkEvent::kConnected, model);
    bridge.OnLinkEvent(LinkEvent::kSecured, model);
  }

  // Sends one message the way the phone does: fragmented for MTU 247.
  void Send(uint8_t type, const std::vector<uint8_t>& payload, uint32_t now_ms = 0) {
    proto::Fragmenter f;
    TEST_ASSERT_TRUE(f.Begin(type, msg_id++, payload.data(), payload.size(), proto::kMinMtu));
    std::vector<uint8_t> frame(proto::kMinMtu - 3);
    size_t n = 0;
    while ((n = f.Next(frame.data(), frame.size())) > 0) {
      bridge.OnFrame(frame.data(), n, now_ms, model, out);
    }
  }

  void SendJson(uint8_t type, const std::string& json, uint32_t now_ms = 0) {
    Send(type, std::vector<uint8_t>(json.begin(), json.end()), now_ms);
  }

  void Hello() { SendJson(0x01, R"({"t":"hello","proto":[1,0],"app":"0.1.0","caps":["osm"]})"); }

  AppModel model;
  ProtocolBridge bridge;
  RecordingOutbox out;
  uint8_t msg_id = 0;
};

std::string VectorPayload(const char* name) {
  const protocol_vectors::Doc doc = protocol_vectors::Load("json_messages.json");
  for (const int c : protocol_vectors::Elements(doc, protocol_vectors::Need(doc, 0, "valid"))) {
    if (protocol_vectors::Text(doc, protocol_vectors::Need(doc, c, "name")) == name) {
      return protocol_vectors::String(doc, protocol_vectors::Need(doc, c, "canonical"));
    }
  }
  TEST_FAIL_MESSAGE(name);
  return {};
}

std::vector<uint8_t> BinaryVector(const char* name) {
  const protocol_vectors::Doc doc = protocol_vectors::Load("binary_messages.json");
  for (const int c : protocol_vectors::Elements(doc, protocol_vectors::Need(doc, 0, "valid"))) {
    if (protocol_vectors::Text(doc, protocol_vectors::Need(doc, c, "name")) == name) {
      return protocol_vectors::Hex(
          protocol_vectors::Text(doc, protocol_vectors::Need(doc, c, "hex")));
    }
  }
  TEST_FAIL_MESSAGE(name);
  return {};
}

void TestHelloRepliesHelloFlowStyle() {
  Fixture f;
  TEST_ASSERT_EQUAL(static_cast<int>(LinkStatus::kWaitingForApp),
                    static_cast<int>(f.model.State().phone.link));
  f.Hello();
  TEST_ASSERT_EQUAL(static_cast<int>(LinkStatus::kReady),
                    static_cast<int>(f.model.State().phone.link));
  TEST_ASSERT_TRUE(f.model.State().phone.connected);
  TEST_ASSERT_EQUAL(3, f.out.sent.size());
  TEST_ASSERT_EQUAL(0x01, f.out.sent[0].type);
  TEST_ASSERT_EQUAL_STRING(
      R"({"t":"hello","proto":[1,1],"fw":"0.0.0-dev","seq":0,"caps":{"styles":["classic","hud","racing"],"chunk_kb":0,"max_msg":15360},"route":null,"epoch":null})",
      f.out.sent[0].text.c_str());
  TEST_ASSERT_EQUAL_STRING(R"({"t":"flow","window":32,"consumed":1})", f.out.sent[1].text.c_str());
  TEST_ASSERT_EQUAL_STRING(R"({"t":"style","map":"classic","layers":[]})",
                           f.out.sent[2].text.c_str());
}

void TestMajorMismatch() {
  Fixture f;
  f.SendJson(0x01, R"({"t":"hello","proto":[2,0],"app":"9.0.0","caps":[]})");
  TEST_ASSERT_EQUAL(static_cast<int>(LinkStatus::kUpdateFirmware),
                    static_cast<int>(f.model.State().phone.link));
  TEST_ASSERT_EQUAL(1, f.out.sent.size());  // our hello, so the app can say so too; no flow
  f.SendJson(0x07, VectorPayload("nav"));
  TEST_ASSERT_FALSE(f.model.State().nav.active);
  Fixture g;
  g.SendJson(0x01, R"({"t":"hello","proto":[0,9],"app":"0.0.1","caps":[]})");
  TEST_ASSERT_EQUAL(static_cast<int>(LinkStatus::kUpdateApp),
                    static_cast<int>(g.model.State().phone.link));
}

void TestNothingCountsBeforeHello() {
  Fixture f;
  f.SendJson(0x07, VectorPayload("nav"));
  TEST_ASSERT_FALSE(f.model.State().nav.active);
  TEST_ASSERT_EQUAL(1, f.bridge.Stats().ignored);
}

void TestUnsecuredFramesAreIgnored() {
  Fixture f;
  f.bridge.OnLinkEvent(LinkEvent::kAdvertising, f.model);  // disconnected
  f.bridge.OnLinkEvent(LinkEvent::kConnected, f.model);
  f.Hello();
  TEST_ASSERT_EQUAL(0, f.out.sent.size());
  TEST_ASSERT_EQUAL(static_cast<int>(LinkStatus::kSecuring),
                    static_cast<int>(f.model.State().phone.link));
}

void TestNavFromVectors() {
  Fixture f;
  f.Hello();
  f.SendJson(0x07, VectorPayload("nav-roundabout"));
  const auto& nav = f.model.State().nav;
  TEST_ASSERT_TRUE(nav.active);
  TEST_ASSERT_EQUAL(static_cast<int>(ManeuverType::kRoundabout), static_cast<int>(nav.next.type));
  TEST_ASSERT_EQUAL(2, nav.next.roundabout_exit);
  TEST_ASSERT_EQUAL(80, nav.distance_m);
  TEST_ASSERT_EQUAL(300, nav.eta_s);
  TEST_ASSERT_TRUE(nav.rerouting);
  TEST_ASSERT_FALSE(nav.off_route);
  f.SendJson(0x07, VectorPayload("nav-unknown-maneuver"));
  TEST_ASSERT_EQUAL(static_cast<int>(ManeuverType::kStraight), static_cast<int>(nav.next.type));
  TEST_ASSERT_FALSE(nav.rerouting);
  f.SendJson(
      0x07,
      R"({"t":"nav","m":"fork_right","exit":0,"dist_m":5,"street":"A\tB","rem_s":1,"rem_m":1,"flags":["off_route"]})");
  TEST_ASSERT_EQUAL(static_cast<int>(ManeuverType::kSlightRight), static_cast<int>(nav.next.type));
  TEST_ASSERT_EQUAL_STRING("A B", nav.street.CStr());
  TEST_ASSERT_TRUE(nav.off_route);
  f.SendJson(0x08, VectorPayload("nav-end"));
  TEST_ASSERT_FALSE(nav.active);
}

void TestGuidanceGoesStale() {
  Fixture f;
  f.Hello();
  f.SendJson(0x07, VectorPayload("nav"), 1000);
  f.bridge.Tick(1000 + kNavStaleMs - 1, f.model, f.out);
  TEST_ASSERT_FALSE(f.model.State().nav.guidance_stale);
  f.bridge.Tick(1000 + kNavStaleMs, f.model, f.out);
  TEST_ASSERT_TRUE(f.model.State().nav.guidance_stale);
  f.SendJson(0x07, VectorPayload("nav"), 1000 + kNavStaleMs + 5);
  TEST_ASSERT_FALSE(f.model.State().nav.guidance_stale);
}

void TestCallMediaBatteryBackend() {
  Fixture f;
  f.Hello();
  f.SendJson(0x09, VectorPayload("call"), 50);
  TEST_ASSERT_EQUAL(static_cast<int>(CallPhase::kRinging),
                    static_cast<int>(f.model.State().call.phase));
  TEST_ASSERT_EQUAL_STRING("Contact A", f.model.State().call.caller.CStr());
  f.SendJson(0x0A, VectorPayload("media"));
  TEST_ASSERT_TRUE(f.model.State().media.available);
  TEST_ASSERT_EQUAL(268, f.model.State().media.duration_s);
  f.SendJson(0x03, VectorPayload("phone-batt"));
  TEST_ASSERT_EQUAL(77, f.model.State().phone.battery_percent);
  f.SendJson(0x05, VectorPayload("backend-osm-offline"));
  TEST_ASSERT_TRUE(f.model.State().backend.valid);
  TEST_ASSERT_EQUAL_STRING("OSM (offline)", f.model.State().backend.name.CStr());
  TEST_ASSERT_EQUAL_STRING("\xC2\xA9 OpenStreetMap contributors",
                           f.model.State().backend.attribution.CStr());
  TEST_ASSERT_FALSE(f.model.State().backend.switched);  // the first backend isn't a switch
  f.SendJson(0x05, VectorPayload("backend-google"), 900);
  TEST_ASSERT_TRUE(f.model.State().backend.switched);
  TEST_ASSERT_EQUAL(900, f.model.State().backend.switched_ms);
  f.SendJson(0x05, VectorPayload("backend-google"), 950);  // a repeat keeps the notice's start
  TEST_ASSERT_EQUAL(900, f.model.State().backend.switched_ms);
  f.SendJson(0x02, VectorPayload("time"));  // 18:42:05
  f.bridge.Tick(60000, f.model, f.out);
  TEST_ASSERT_EQUAL(18 * 60 + 43, f.model.State().clock.minutes_of_day);
}

void TestCommandsBecomeMessages() {
  Fixture f;
  f.Hello();
  f.out.sent.clear();
  TEST_ASSERT_EQUAL(
      static_cast<int>(LinkAction::kNone),
      static_cast<int>(f.bridge.OnCommand(Command{CommandType::kCallAccept}, 0, f.model, f.out)));
  f.bridge.OnCommand(Command{CommandType::kMediaNext}, 0, f.model, f.out);
  TEST_ASSERT_EQUAL(2, f.out.sent.size());
  TEST_ASSERT_EQUAL(0x13, f.out.sent[0].type);
  TEST_ASSERT_EQUAL_STRING(R"({"t":"call_action","action":"accept"})", f.out.sent[0].text.c_str());
  TEST_ASSERT_EQUAL_STRING(VectorPayload("media-cmd").c_str(), f.out.sent[1].text.c_str());
  TEST_ASSERT_EQUAL(
      static_cast<int>(LinkAction::kForgetPhones),
      static_cast<int>(f.bridge.OnCommand(Command{CommandType::kForgetPhones}, 0, f.model, f.out)));
}

void TestFlowCredit() {
  Fixture f;
  f.Hello();
  f.out.sent.clear();
  // Window/2 frames later, the device grants more credit.
  for (int i = 0; i < kFlowWindow / 2 - 1; ++i) {  // the hello was frame 1
    f.SendJson(0x03, VectorPayload("phone-batt"));
  }
  TEST_ASSERT_EQUAL(0, f.out.sent.size());
  f.SendJson(0x03, VectorPayload("phone-batt"));
  TEST_ASSERT_EQUAL(1, f.out.sent.size());
  TEST_ASSERT_EQUAL_STRING(R"({"t":"flow","window":32,"consumed":17})", f.out.sent[0].text.c_str());
  // Fewer frames: credit after a second.
  f.SendJson(0x03, VectorPayload("phone-batt"), 100);
  f.bridge.Tick(500, f.model, f.out);
  TEST_ASSERT_EQUAL(1, f.out.sent.size());
  f.bridge.Tick(1000, f.model, f.out);
  TEST_ASSERT_EQUAL(2, f.out.sent.size());
  TEST_ASSERT_EQUAL_STRING(R"({"t":"flow","window":32,"consumed":18})", f.out.sent[1].text.c_str());
}

void TestStyleFollowsTheTheme() {
  Fixture f;
  f.Hello();
  f.out.sent.clear();
  TEST_ASSERT_TRUE(f.model.SetTheme(sarathi::core::ThemeId::kRacing));
  f.bridge.Tick(0, f.model, f.out);
  TEST_ASSERT_EQUAL(1, f.out.sent.size());
  TEST_ASSERT_EQUAL_STRING(R"({"t":"style","map":"racing","layers":[]})",
                           f.out.sent[0].text.c_str());
}

void TestPoseNeedsTheFrame() {
  Fixture f;
  f.Hello();
  const auto pose = BinaryVector("pose");  // epoch 7, 1389 cm/s
  f.Send(0x40, pose);
  TEST_ASSERT_EQUAL(1, f.bridge.Stats().messages_dropped);
  TEST_ASSERT_EQUAL_STRING("epoch", f.bridge.Stats().last_drop_reason);
  f.SendJson(0x04, VectorPayload("frame"));  // epoch 7
  f.Send(0x40, pose);
  TEST_ASSERT_EQUAL(50, f.model.State().ride.speed_kmh);
  TEST_ASSERT_FALSE(f.model.State().ride.moving);  // one sample: not yet 2 s
}

std::vector<uint8_t> PoseAt(uint16_t speed_cms) {
  proto::Pose pose;
  pose.epoch = 7;
  pose.speed_cms = speed_cms;
  std::vector<uint8_t> bytes(proto::kPoseSize);
  TEST_ASSERT_EQUAL(proto::kPoseSize, proto::EncodeBinary(pose, bytes.data(), bytes.size()));
  return bytes;
}

// The lock follows pose speed with hysteresis; without poses it falls back to "locked while
// navigating"; the phone can change the thresholds or turn it off (ADR-0011).
void TestMovingLockFromPoses() {
  Fixture f;
  f.Hello();
  f.SendJson(0x04, VectorPayload("frame"));
  f.SendJson(0x07, VectorPayload("nav"), 0);
  TEST_ASSERT_TRUE(f.model.SettingsLocked());  // navigating, no speed yet
  f.Send(0x40, PoseAt(0), 100);
  TEST_ASSERT_FALSE(f.model.SettingsLocked());  // speed known: standing still
  for (uint32_t t = 1000; t <= 3000; t += 1000) {
    f.Send(0x40, PoseAt(1000), t);  // 36 km/h
  }
  TEST_ASSERT_TRUE(f.model.State().ride.moving);
  TEST_ASSERT_TRUE(f.model.SettingsLocked());
  f.bridge.Tick(3000 + sarathi::core::kSpeedTimeoutMs, f.model, f.out);  // poses stop
  TEST_ASSERT_FALSE(f.model.State().ride.speed_valid);
  TEST_ASSERT_FALSE(f.model.State().ride.moving);
  TEST_ASSERT_TRUE(f.model.SettingsLocked());  // still navigating: locked
  f.SendJson(0x0B, VectorPayload("lock-off"));
  TEST_ASSERT_FALSE(f.model.SettingsLocked());
  f.SendJson(0x0B, VectorPayload("lock-default"));
  TEST_ASSERT_TRUE(f.model.SettingsLocked());
}

void TestHelloReportsTheHeldRoute() {
  Fixture f;
  f.Hello();
  f.SendJson(0x04, VectorPayload("frame"));
  f.Send(0x41, BinaryVector("route"));  // route 3 v2, epoch 7
  TEST_ASSERT_EQUAL(0, f.bridge.Stats().messages_dropped);
  // Reconnect: the route and frame survive, live data doesn't.
  f.SendJson(0x07, VectorPayload("nav"));
  f.bridge.OnLinkEvent(LinkEvent::kAdvertising, f.model);
  TEST_ASSERT_FALSE(f.model.State().nav.active);
  TEST_ASSERT_FALSE(f.model.State().phone.connected);
  TEST_ASSERT_EQUAL(static_cast<int>(LinkStatus::kWaiting),
                    static_cast<int>(f.model.State().phone.link));
  f.bridge.OnLinkEvent(LinkEvent::kConnected, f.model);
  f.bridge.OnLinkEvent(LinkEvent::kSecured, f.model);
  f.out.sent.clear();
  f.Hello();
  TEST_ASSERT_TRUE(f.out.sent[0].text.find(R"("route":{"id":3,"v":2},"epoch":7})") !=
                   std::string::npos);
}

// route + pose make the map's window; a new frame forgets them.
void TestPoseOnTheRouteMakesAWindow() {
  Fixture f;
  f.Hello();
  f.SendJson(0x04, VectorPayload("frame"));
  f.Send(0x41, BinaryVector("route"));  // (0,0) (120,5) (240,300) ... in 1 m quanta
  proto::Pose pose;
  pose.epoch = 7;
  pose.x_dm = 600;  // 60 m east, 2.5 m north: on the first segment
  pose.y_dm = 25;
  pose.speed_cms = 50;  // too slow for the phone's heading: the route's is used
  std::vector<uint8_t> bytes(proto::kPoseSize);
  TEST_ASSERT_EQUAL(proto::kPoseSize, proto::EncodeBinary(pose, bytes.data(), bytes.size()));
  f.Send(0x40, bytes, 1234);
  const auto& nav = f.model.State().nav;
  TEST_ASSERT_TRUE(nav.has_window);
  TEST_ASSERT_EQUAL(1234, nav.window_received_ms);
  TEST_ASSERT_EQUAL(88, nav.window.heading_deg);  // along (120, 5): just north of east
  TEST_ASSERT_TRUE(nav.window.count >= 2);
  TEST_ASSERT_EQUAL(-60, nav.window.points[0].x_m);  // the route's start, behind the rider
  TEST_ASSERT_TRUE(nav.window.has_maneuver_point);   // marker at point 2: left
  TEST_ASSERT_EQUAL(180, nav.window.maneuver_point.x_m);
  // Five poses a second, one window a second.
  const uint32_t sequence = nav.window.sequence;
  f.Send(0x40, bytes, 1234 + 999);
  TEST_ASSERT_EQUAL(sequence, nav.window.sequence);
  TEST_ASSERT_EQUAL(1234, nav.window_received_ms);
  f.Send(0x40, bytes, 1234 + 1000);
  TEST_ASSERT_EQUAL(sequence + 1, nav.window.sequence);
  f.SendJson(0x04, R"({"t":"frame","epoch":8})");
  TEST_ASSERT_FALSE(nav.has_window);
}

void TestBadMessagesAreCounted() {
  Fixture f;
  f.Hello();
  f.SendJson(0x03, R"({"t":"phone_batt","level":101,"charging":true})");
  TEST_ASSERT_EQUAL(1, f.bridge.Stats().messages_dropped);
  TEST_ASSERT_EQUAL_STRING("invalid", f.bridge.Stats().last_drop_reason);
  const uint8_t stray[] = {0x07, 0x01, 0x01, 0x00};  // NOLINT(*-avoid-c-arrays)
  f.bridge.OnFrame(stray, sizeof(stray), 0, f.model, f.out);
  TEST_ASSERT_EQUAL(1, f.bridge.Stats().frames_dropped);
  f.Send(0x44, {1, 0, 0, 0});  // chunk_have is the device's, never accepted
  f.Send(0x50, {1});           // an unknown type
  TEST_ASSERT_EQUAL(2, f.bridge.Stats().ignored);
}

}  // namespace

int main() {
  UNITY_BEGIN();
  RUN_TEST(TestHelloRepliesHelloFlowStyle);
  RUN_TEST(TestMajorMismatch);
  RUN_TEST(TestNothingCountsBeforeHello);
  RUN_TEST(TestUnsecuredFramesAreIgnored);
  RUN_TEST(TestNavFromVectors);
  RUN_TEST(TestGuidanceGoesStale);
  RUN_TEST(TestCallMediaBatteryBackend);
  RUN_TEST(TestCommandsBecomeMessages);
  RUN_TEST(TestFlowCredit);
  RUN_TEST(TestStyleFollowsTheTheme);
  RUN_TEST(TestPoseNeedsTheFrame);
  RUN_TEST(TestMovingLockFromPoses);
  RUN_TEST(TestHelloReportsTheHeldRoute);
  RUN_TEST(TestPoseOnTheRouteMakesAWindow);
  RUN_TEST(TestBadMessagesAreCounted);
  return UNITY_END();
}
