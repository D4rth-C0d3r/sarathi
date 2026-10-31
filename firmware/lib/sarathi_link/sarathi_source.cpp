// Only in the phone build: NimBLE isn't a dependency of the others.
#ifdef SARATHI_BACKEND_PHONE

#include "sarathi_source.h"

#include <array>
#include <atomic>
#include <cstring>
#include <new>

#include <NimBLEDevice.h>
#include <esp32-hal-log.h>  // ESP_LOGx through Arduino's logger (CLAUDE.md)
#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include <freertos/idf_additions.h>
#include <freertos/queue.h>
#include <freertos/task.h>

#include "frame.h"
#include "json_codec.h"
#include "protocol_bridge.h"

namespace sarathi::link {
namespace {

constexpr const char* kTag = "link";
// GATT service and characteristics (protocol.md section 1, ADR-0007).
constexpr const char* kServiceUuid = "831fd13f-67ea-432e-b7b7-09926c4b92a6";
constexpr const char* kRxUuid = "cd32526c-d882-496f-b7c1-301a1fdf68a7";
constexpr const char* kTxUuid = "9ae416fd-2b66-43f5-b902-001a23a238a9";

constexpr uint32_t kStackBytes = 4096;
constexpr UBaseType_t kPriority = 1;
constexpr BaseType_t kCore = 0;  // with the BLE stack; the UI runs on core 1
constexpr uint32_t kLinkPeriodMs = 10;
constexpr uint16_t kMaxMtu = 517;
constexpr size_t kMaxFrame = kMaxMtu - proto::kAttHeader;
// The flow window plus room for the phone's hello and a burst of single-frame messages sent
// between a chunk's fragments.
constexpr UBaseType_t kRxQueueLength = kFlowWindow + 16;
constexpr UBaseType_t kTxQueueLength = 8;
constexpr UBaseType_t kActionQueueLength = 8;
constexpr size_t kMaxFramesPerPoll = 24;
constexpr size_t kMaxFramesPerTick = 8;  // notifications per link-task pass

struct RxFrame {
  uint32_t generation = 0;  // the connection it came on
  uint16_t size = 0;
  std::array<uint8_t, kMaxFrame> data{};
};

struct TxMessage {
  uint8_t type = 0;
  uint16_t size = 0;
  std::array<uint8_t, proto::kMaxJson> payload{};
};

// NOLINTBEGIN(cppcoreguidelines-avoid-non-const-global-variables)
QueueHandle_t rx_queue = nullptr;
QueueHandle_t tx_queue = nullptr;
QueueHandle_t action_queue = nullptr;
NimBLECharacteristic* tx_characteristic = nullptr;  // link task, and NimBLE's after creation
std::atomic<bool> want_paused{false};
std::atomic<bool> stopped{false};
std::atomic<bool> ble_up{false};
// The phone's connection. A new connection bumps the generation, so frames and events from an
// earlier one are never mistaken for this one's.
std::atomic<uint32_t> generation{0};
std::atomic<uint16_t> conn_handle{0};
std::atomic<bool> connected{false};
std::atomic<bool> secured{false};
std::atomic<bool> subscribed{false};
std::atomic<uint16_t> mtu{0};
std::atomic<bool> mtu_wanted{false};
std::atomic<uint32_t> rx_overflow{0};
std::atomic<uint32_t> rx_refused{0};    // writes from a link that isn't bonded with MITM
std::atomic<uint32_t> extra_phones{0};  // second connections turned away
// Pairing (CLAUDE.md, Security): NimBLE's host task records a request, the UI asks the rider,
// and the link task answers.
std::atomic<uint32_t> pairing_code{0};
std::atomic<uint32_t> pairing_requests{0};
std::atomic<uint32_t> pairing_ends{0};
std::atomic<uint32_t> last_auth{0};  // bit 0 encrypted, 1 bonded, 2 authenticated
std::atomic<uint8_t> paired_phones{0};
RxFrame rx_scratch;                         // NimBLE's host task only
RxFrame poll_frame;                         // UI task only
TxMessage tx_message;                       // link task only: the message being sent
std::array<uint8_t, kMaxFrame> tx_frame{};  // link task only
proto::Fragmenter fragmenter;               // link task only
bool tx_pending_frame = false;              // link task: tx_frame is waiting to go out
size_t tx_frame_size = 0;
uint8_t tx_msg_id = 0;
ProtocolBridge* bridge = nullptr;  // in PSRAM; UI task only after Begin()
// NOLINTEND(cppcoreguidelines-avoid-non-const-global-variables)

bool Secure(const NimBLEConnInfo& info) {
  return info.isEncrypted() && info.isBonded() && info.isAuthenticated();
}

// Copies the write and returns. Only a phone the rider confirmed: encrypted, bonded and
// authenticated (numeric comparison; a "Just Works" bond that never showed a code doesn't
// count). The characteristic's own permissions already demand this; this is the second lock.
class RxCallbacks final : public NimBLECharacteristicCallbacks {
 public:
  void onWrite(NimBLECharacteristic* characteristic, NimBLEConnInfo& info) override {
    const NimBLEAttValue value = characteristic->getValue();
    if (!Secure(info) || info.getConnHandle() != conn_handle.load()) {
      rx_refused.fetch_add(1);
      return;
    }
    if (value.size() == 0 || value.size() > kMaxFrame) {
      return;
    }
    rx_scratch.generation = generation.load();
    rx_scratch.size = static_cast<uint16_t>(value.size());
    std::memcpy(rx_scratch.data.data(), value.data(), value.size());
    if (xQueueSend(rx_queue, &rx_scratch, 0) != pdTRUE) {
      rx_overflow.fetch_add(1);
    }
  }
};

class TxCallbacks final : public NimBLECharacteristicCallbacks {
 public:
  void onSubscribe(NimBLECharacteristic* /*characteristic*/, NimBLEConnInfo& info,
                   uint16_t sub_value) override {
    if (info.getConnHandle() == conn_handle.load()) {
      subscribed.store((sub_value & 1U) != 0 && Secure(info));
    }
  }
};

// One phone at a time. Every phone is asked to secure the link; a phone bonded earlier
// encrypts with its stored keys and never shows a code.
class ServerCallbacks final : public NimBLEServerCallbacks {
 public:
  void onConnect(NimBLEServer* server, NimBLEConnInfo& info) override {
    if (connected.load()) {
      extra_phones.fetch_add(1);
      server->disconnect(info.getConnHandle());
      return;
    }
    generation.fetch_add(1);
    conn_handle.store(info.getConnHandle());
    secured.store(false);
    subscribed.store(false);
    connected.store(true);
    NimBLEDevice::startSecurity(info.getConnHandle());
    mtu_wanted.store(true);
  }
  void onDisconnect(NimBLEServer* /*server*/, NimBLEConnInfo& info, int /*reason*/) override {
    if (info.getConnHandle() != conn_handle.load() || !connected.load()) {
      return;  // a turned-away second phone
    }
    connected.store(false);
    secured.store(false);
    subscribed.store(false);
    pairing_ends.fetch_add(1);
  }
  void onMTUChange(uint16_t mtu_bytes, NimBLEConnInfo& info) override {
    if (info.getConnHandle() == conn_handle.load()) {
      mtu.store(mtu_bytes);
    }
  }
  void onConfirmPassKey(NimBLEConnInfo& /*info*/, uint32_t pin) override {
    pairing_code.store(pin);
    pairing_requests.fetch_add(1);
  }
  void onAuthenticationComplete(NimBLEConnInfo& info) override {
    last_auth.store((info.isEncrypted() ? 1U : 0U) | (info.isBonded() ? 2U : 0U) |
                    (info.isAuthenticated() ? 4U : 0U));
    pairing_ends.fetch_add(1);
    if (info.getConnHandle() == conn_handle.load()) {
      secured.store(Secure(info));
      if (!Secure(info)) {
        NimBLEDevice::getServer()->disconnect(info.getConnHandle());  // failed or Just Works
      }
    }
  }
};

// NOLINTBEGIN(cppcoreguidelines-avoid-non-const-global-variables)
RxCallbacks rx_callbacks;
TxCallbacks tx_callbacks;
ServerCallbacks server_callbacks;
// NOLINTEND(cppcoreguidelines-avoid-non-const-global-variables)

void CountBonds() {
  const int bonds = NimBLEDevice::getNumBonds();
  paired_phones.store(static_cast<uint8_t>(bonds < 0 ? 0 : bonds));
}

// The service, its two characteristics, and advertising. Both characteristics need an
// encrypted, authenticated link at the ATT level, so nothing moves before pairing.
bool StartBle(const char* name) {
  if (!NimBLEDevice::init(name)) {
    return false;
  }
  NimBLEDevice::setMTU(kMaxMtu);
  NimBLEDevice::setSecurityAuth(true, true, true);          // bonding, MITM, secure connections
  NimBLEDevice::setSecurityIOCap(BLE_HS_IO_DISPLAY_YESNO);  // the code on the dash, a knob yes/no
  NimBLEServer* server = NimBLEDevice::createServer();
  server->setCallbacks(&server_callbacks, false);
  server->advertiseOnDisconnect(true);
  NimBLEService* service = server->createService(kServiceUuid);
  NimBLECharacteristic* rx = service->createCharacteristic(
      kRxUuid,
      // Write for the phone's hello, Write Without Response for the rest (protocol.md).
      NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR | NIMBLE_PROPERTY::WRITE_ENC |
          NIMBLE_PROPERTY::WRITE_AUTHEN,
      kMaxFrame);
  tx_characteristic = service->createCharacteristic(
      kTxUuid, NIMBLE_PROPERTY::NOTIFY | NIMBLE_PROPERTY::READ_ENC | NIMBLE_PROPERTY::READ_AUTHEN,
      kMaxFrame);
  if (rx == nullptr || tx_characteristic == nullptr) {
    return false;
  }
  rx->setCallbacks(&rx_callbacks);
  tx_characteristic->setCallbacks(&tx_callbacks);
  NimBLEAdvertising* advertising = NimBLEDevice::getAdvertising();
  advertising->setName(name);
  advertising->addServiceUUID(NimBLEUUID(kServiceUuid));
  advertising->enableScanResponse(true);
  CountBonds();
  return server->start() && advertising->start();
}

void StopBle() {
  NimBLEDevice::deinit(true);  // frees the BLE stack's RAM for Wi-Fi
  tx_characteristic = nullptr;
  connected.store(false);
  secured.store(false);
  subscribed.store(false);
  tx_pending_frame = false;
  xQueueReset(tx_queue);
}

void AnswerPairing(bool accept) {
  NimBLEServer* server = NimBLEDevice::getServer();
  if (server != nullptr && connected.load()) {
    NimBLEDevice::injectConfirmPasskey(server->getPeerInfoByHandle(conn_handle.load()), accept);
  }
}

void ForgetPhones() {
  NimBLEDevice::deleteAllBonds();
  NimBLEServer* server = NimBLEDevice::getServer();
  if (server != nullptr) {
    for (const uint16_t handle : server->getPeerDevices()) {
      server->disconnect(handle);
    }
  }
  CountBonds();
}

void Act(LinkAction action) {
  switch (action) {
    case LinkAction::kPairAccept:
      AnswerPairing(true);
      break;
    case LinkAction::kPairReject:
      AnswerPairing(false);
      break;
    case LinkAction::kForgetPhones:
      ForgetPhones();
      break;
    case LinkAction::kNone:
      break;
  }
}

// Fragments queued messages at the current MTU and notifies them, a few frames per pass. A
// frame the stack can't take now (no buffers) is retried on the next pass. Nothing goes out
// unless the link is secured, subscribed and its MTU raised (protocol.md section 5).
void SendFrames() {
  if (!secured.load() || !subscribed.load() || mtu.load() < proto::kMinMtu ||
      tx_characteristic == nullptr) {
    return;
  }
  for (size_t sent = 0; sent < kMaxFramesPerTick; ++sent) {
    if (!tx_pending_frame) {
      tx_frame_size = fragmenter.Next(tx_frame.data(), tx_frame.size());
      if (tx_frame_size == 0) {
        if (xQueueReceive(tx_queue, &tx_message, 0) != pdTRUE ||
            !fragmenter.Begin(tx_message.type, tx_msg_id++, tx_message.payload.data(),
                              tx_message.size, mtu.load())) {
          return;
        }
        tx_frame_size = fragmenter.Next(tx_frame.data(), tx_frame.size());
      }
      tx_pending_frame = tx_frame_size > 0;
    }
    if (!tx_pending_frame ||
        !tx_characteristic->notify(tx_frame.data(), tx_frame_size, conn_handle.load())) {
      return;
    }
    tx_pending_frame = false;
  }
}

void LinkTask(void* arg) {
  const auto* name = static_cast<const char*>(arg);
  uint32_t bonds_counted_at = 0;
  uint32_t generation_sent = 0;
  for (;;) {
    if (want_paused.load() && !stopped.load()) {
      StopBle();
      ble_up.store(false);
      stopped.store(true);
      ESP_LOGI(kTag, "BLE stopped for Wi-Fi");
    } else if (!want_paused.load() && stopped.load()) {
      if (StartBle(name)) {
        stopped.store(false);
        ble_up.store(true);
        ESP_LOGI(kTag, "BLE restarted");
      } else {
        ESP_LOGE(kTag, "BLE failed to restart");
      }
    }
    LinkAction action = LinkAction::kNone;
    if (stopped.load()) {
      while (xQueueReceive(action_queue, &action, 0) == pdTRUE) {
      }
    } else {
      // Either side may start the MTU exchange; Android answers with its maximum at once
      // (docs/phase3-findings.md).
      if (mtu_wanted.exchange(false)) {
        const int rc = ble_gattc_exchange_mtu(conn_handle.load(), nullptr, nullptr);
        if (rc != 0) {
          ESP_LOGW(kTag, "MTU exchange not started (rc %d)", rc);
        }
      }
      if (generation.load() != generation_sent) {
        generation_sent = generation.load();  // a new phone: nothing old goes to it
        tx_pending_frame = false;
        fragmenter = proto::Fragmenter{};
        xQueueReset(tx_queue);
      }
      if (pairing_ends.load() != bonds_counted_at) {
        bonds_counted_at = pairing_ends.load();
        CountBonds();
      }
      while (xQueueReceive(action_queue, &action, 0) == pdTRUE) {
        Act(action);
      }
      SendFrames();
    }
    vTaskDelay(pdMS_TO_TICKS(kLinkPeriodMs));
  }
}

// The bridge's messages go to the link task, copied.
class QueueOutbox final : public Outbox {
 public:
  bool Send(uint8_t type, const uint8_t* payload, size_t size) override {
    if (size > scratch_.payload.size() || tx_queue == nullptr) {
      return false;
    }
    scratch_.type = type;
    scratch_.size = static_cast<uint16_t>(size);
    std::memcpy(scratch_.payload.data(), payload, size);
    return xQueueSend(tx_queue, &scratch_, 0) == pdTRUE;
  }

 private:
  TxMessage scratch_;  // UI task only
};

QueueOutbox outbox;  // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)

template <typename T>
T* PsramArray(size_t n) {
  return static_cast<T*>(heap_caps_calloc(n, sizeof(T), MALLOC_CAP_SPIRAM));
}

}  // namespace

bool SarathiSource::Begin(const char* name) {
  rx_queue = xQueueCreateWithCaps(kRxQueueLength, sizeof(RxFrame), MALLOC_CAP_SPIRAM);
  tx_queue = xQueueCreateWithCaps(kTxQueueLength, sizeof(TxMessage), MALLOC_CAP_SPIRAM);
  action_queue = xQueueCreate(kActionQueueLength, sizeof(LinkAction));
  BridgeStorage storage;
  storage.reassembly = PsramArray<uint8_t>(proto::kMaxMessage);
  storage.reassembly_size = proto::kMaxMessage;
  storage.route_points = PsramArray<proto::Point>(proto::kMaxRoutePoints);
  storage.route_points_size = proto::kMaxRoutePoints;
  storage.tracker = {PsramArray<core::MetricPoint>(proto::kMaxRoutePoints),
                     PsramArray<float>(proto::kMaxRoutePoints), proto::kMaxRoutePoints};
  void* memory = heap_caps_calloc(1, sizeof(ProtocolBridge), MALLOC_CAP_SPIRAM);
  if (rx_queue == nullptr || tx_queue == nullptr || action_queue == nullptr ||
      storage.reassembly == nullptr || storage.route_points == nullptr ||
      storage.tracker.points == nullptr || storage.tracker.cumulative == nullptr ||
      memory == nullptr) {
    ESP_LOGE(kTag, "out of memory");
    return false;
  }
  bridge = new (memory) ProtocolBridge(storage);
  if (!StartBle(name)) {
    ESP_LOGE(kTag, "BLE failed to start");
    return false;
  }
  ble_up.store(true);
  // `name` is a string literal from main.cpp, so it outlives the task.
  if (xTaskCreatePinnedToCore(LinkTask, "link", kStackBytes, const_cast<char*>(name), kPriority,
                              nullptr, kCore) != pdPASS) {
    ESP_LOGE(kTag, "link task failed to start");
    return false;
  }
  ESP_LOGI(kTag, "advertising as %s (protocol %u.%u)", name,
           static_cast<unsigned>(proto::kProtoMajor), static_cast<unsigned>(proto::kProtoMinor));
  return true;
}

void SarathiSource::SetBlePaused(bool paused) { want_paused.store(paused); }

bool SarathiSource::BleStopped() const { return stopped.load(); }

bool SarathiSource::BleUp() const { return ble_up.load(); }

void SarathiSource::Poll(uint32_t now_ms, core::AppModel& model) {
  if (bridge == nullptr) {
    return;
  }
  if (!started_) {
    bridge->Begin(model);
    started_ = true;
  }
  // The link state first, so frames are only fed to the connection they came on.
  PollLink(model);
  for (size_t i = 0; i < kMaxFramesPerPoll && xQueueReceive(rx_queue, &poll_frame, 0) == pdTRUE;
       ++i) {
    if (poll_frame.generation == generation_seen_ && secured_seen_) {
      bridge->OnFrame(poll_frame.data.data(), poll_frame.size, now_ms, model, outbox);
    }
  }
  PollPairing(now_ms, model);
  bridge->Tick(now_ms, model, outbox);
  LogStats();
}

// Reports what changed since the last poll, in order: a lost or replaced connection first.
void SarathiSource::PollLink(core::AppModel& model) {
  const bool now_stopped = stopped.load();
  if (now_stopped != stopped_seen_) {
    stopped_seen_ = now_stopped;
    bridge->OnLinkEvent(now_stopped ? LinkEvent::kStopped : LinkEvent::kAdvertising, model);
    connected_seen_ = false;
    secured_seen_ = false;
  }
  if (now_stopped) {
    return;
  }
  const uint32_t now_generation = generation.load();
  const bool now_connected = connected.load();
  if (now_generation != generation_seen_ || (connected_seen_ && !now_connected)) {
    if (connected_seen_) {
      bridge->OnLinkEvent(LinkEvent::kAdvertising, model);
      ESP_LOGI(kTag, "phone disconnected");
    }
    generation_seen_ = now_generation;
    connected_seen_ = false;
    secured_seen_ = false;
  }
  if (now_connected && !connected_seen_) {
    connected_seen_ = true;
    bridge->OnLinkEvent(LinkEvent::kConnected, model);
    ESP_LOGI(kTag, "phone connected");
  }
  if (now_connected && secured.load() && !secured_seen_) {
    secured_seen_ = true;
    bridge->OnLinkEvent(LinkEvent::kSecured, model);
    ESP_LOGI(kTag, "link secured");
  }
}

void SarathiSource::PollPairing(uint32_t now_ms, core::AppModel& model) {
  if (pairing_requests.load() != pairing_requests_seen_) {
    pairing_requests_seen_ = pairing_requests.load();
    ProtocolBridge::OnPairingRequest(pairing_code.load(), now_ms, model);
    ESP_LOGI(kTag, "a phone asks to pair: confirm the code on the dash");  // never the code
  }
  if (pairing_ends.load() != pairing_ends_seen_) {
    pairing_ends_seen_ = pairing_ends.load();
    ProtocolBridge::OnPairingEnded(model);
    const uint32_t auth = last_auth.load();
    ESP_LOGI(kTag, "link security: encrypted %u, bonded %u, authenticated %u", auth & 1U,
             (auth >> 1U) & 1U, (auth >> 2U) & 1U);
  }
  if (paired_phones.load() != model.State().phone.paired_phones) {
    ProtocolBridge::OnPairedPhones(paired_phones.load(), model);
  }
}

void SarathiSource::LogStats() {
  if (mtu.load() != mtu_logged_) {
    mtu_logged_ = mtu.load();
    ESP_LOGI(kTag, "MTU %u", static_cast<unsigned>(mtu_logged_));
  }
  if (rx_refused.load() + extra_phones.load() != refused_logged_) {
    refused_logged_ = rx_refused.load() + extra_phones.load();
    ESP_LOGW(kTag, "%u write(s) refused from an unpaired link, %u extra phone(s) turned away",
             static_cast<unsigned>(rx_refused.load()), static_cast<unsigned>(extra_phones.load()));
  }
  const BridgeStats& s = bridge->Stats();
  const uint32_t dropped = s.frames_dropped + s.messages_dropped + s.send_failed + rx_overflow;
  if (dropped != dropped_logged_) {
    dropped_logged_ = dropped;
    ESP_LOGW(kTag,
             "dropped so far: %u frame(s) (last: %s), %u message(s) (last: type 0x%02X, %s), "
             "%u send(s) failed, %u queue overflow(s)",
             static_cast<unsigned>(s.frames_dropped), proto::DropName(s.last_frame_drop),
             static_cast<unsigned>(s.messages_dropped), s.last_drop_type, s.last_drop_reason,
             static_cast<unsigned>(s.send_failed), static_cast<unsigned>(rx_overflow.load()));
  }
}

void SarathiSource::OnCommand(const core::Command& command, uint32_t now_ms,
                              core::AppModel& model) {
  if (bridge == nullptr) {
    return;
  }
  const LinkAction action = bridge->OnCommand(command, now_ms, model, outbox);
  if (action != LinkAction::kNone && action_queue != nullptr) {
    xQueueSend(action_queue, &action, 0);
  }
}

}  // namespace sarathi::link

#endif  // SARATHI_BACKEND_PHONE
