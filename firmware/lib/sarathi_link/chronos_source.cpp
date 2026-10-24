// Only in the Chronos build: the library and NimBLE aren't dependencies of the others.
#ifdef SARATHI_BACKEND_CHRONOS

#include "chronos_source.h"

#include <array>
#include <atomic>
#include <cstring>
#include <new>

#include <ChronosESP32.h>
#include <NimBLEDevice.h>
#include <esp32-hal-log.h>  // ESP_LOGx through Arduino's logger (CLAUDE.md)
#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include <freertos/idf_additions.h>
#include <freertos/queue.h>
#include <freertos/task.h>

namespace sarathi::link {
namespace {

constexpr const char* kTag = "link";
constexpr uint32_t kStackBytes = 6144;  // the library builds Arduino Strings in loop()
constexpr UBaseType_t kPriority = 1;
constexpr BaseType_t kCore = 0;  // with the BLE stack; the UI runs on core 1
constexpr uint32_t kLinkPeriodMs = 20;
constexpr UBaseType_t kRxQueueLength = 32;  // bursts: the app sends a notification backlog
constexpr UBaseType_t kActionQueueLength = 8;
constexpr size_t kMaxWritesPerPoll = 16;
constexpr uint8_t kDeviceBatteryPercent = 100;  // powered by the bike; Chronos shows it

struct Write {
  uint16_t size = 0;
  std::array<uint8_t, kChronosMaxPacket> data{};
};

// NOLINTBEGIN(cppcoreguidelines-avoid-non-const-global-variables)
ChronosESP32* watch = nullptr;  // in PSRAM; only the link task calls it after Begin()
QueueHandle_t rx_queue = nullptr;
QueueHandle_t action_queue = nullptr;
std::atomic<bool> want_paused{false};
std::atomic<bool> stopped{false};
std::atomic<bool> connected{false};
std::atomic<bool> ble_up{false};
std::atomic<uint32_t> rx_overflow{0};
std::atomic<uint16_t> mtu{0};
Write poll_write;  // UI task only: too big for its stack
// Pairing (CLAUDE.md, Security): NimBLE's host task records a request, the UI asks the rider,
// and the link task answers. Each counter changes once per event.
std::atomic<uint32_t> pairing_code{0};
std::atomic<uint16_t> pairing_handle{0};
std::atomic<uint32_t> pairing_requests{0};
std::atomic<uint32_t> pairing_ends{0};
std::atomic<uint32_t> last_auth{0};  // bit 0 encrypted, 1 bonded, 2 authenticated
std::atomic<uint8_t> paired_phones{0};
std::atomic<uint32_t> rx_refused{0};  // writes from a link that isn't bonded with MITM
// A new connection whose MTU the link task should raise (see LinkTask).
std::atomic<uint16_t> mtu_handle{0};
std::atomic<bool> mtu_wanted{false};
// NOLINTEND(cppcoreguidelines-avoid-non-const-global-variables)

// Replaces the library's RX handler, so its parser (which trusts packet lengths and indices,
// docs/phase3-findings.md) never runs. Copies the write and returns: no parsing, logging or
// LVGL on NimBLE's host task.
class RxCallbacks final : public NimBLECharacteristicCallbacks {
 public:
  void onWrite(NimBLECharacteristic* characteristic, NimBLEConnInfo& info) override {
    // NimBLE hands the value out by copy: one short-lived allocation inside NimBLE per write.
    const NimBLEAttValue value = characteristic->getValue();
    mtu.store(info.getMTU());
    // Only a phone the rider confirmed: encrypted, bonded, and authenticated (numeric
    // comparison, so a "Just Works" bond that never showed a code doesn't count).
    if (!info.isEncrypted() || !info.isBonded() || !info.isAuthenticated()) {
      rx_refused.fetch_add(1);
      return;
    }
    if (value.size() == 0 || value.size() > kChronosMaxPacket) {
      return;
    }
    scratch_.size = static_cast<uint16_t>(value.size());
    std::memcpy(scratch_.data.data(), value.data(), value.size());
    if (xQueueSend(rx_queue, &scratch_, 0) != pdTRUE) {
      rx_overflow.fetch_add(1);
    }
  }

 private:
  Write scratch_;  // NimBLE's host task only
};

RxCallbacks rx_callbacks;  // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)

// Takes over the library's server callbacks: forwards connect and disconnect to it (through
// its public base class, which is legal C++ and needs no edit to the library), asks every
// phone to secure the link, and hands a pairing code to the UI instead of accepting it. A
// phone bonded earlier encrypts with its stored keys and never gets here. Copies and returns:
// no logging or LVGL on NimBLE's host task.
class ServerProxy final : public NimBLEServerCallbacks {
 public:
  void onConnect(NimBLEServer* server, NimBLEConnInfo& info) override {
    static_cast<NimBLEServerCallbacks*>(watch)->onConnect(server, info);
    NimBLEDevice::startSecurity(info.getConnHandle());
    mtu_handle.store(info.getConnHandle());
    mtu_wanted.store(true);
  }
  void onDisconnect(NimBLEServer* server, NimBLEConnInfo& info, int reason) override {
    static_cast<NimBLEServerCallbacks*>(watch)->onDisconnect(server, info, reason);
    pairing_ends.fetch_add(1);
  }
  void onMTUChange(uint16_t mtu_bytes, NimBLEConnInfo& /*info*/) override { mtu.store(mtu_bytes); }
  void onConfirmPassKey(NimBLEConnInfo& info, uint32_t pin) override {
    pairing_code.store(pin);
    pairing_handle.store(info.getConnHandle());
    pairing_requests.fetch_add(1);
  }
  void onAuthenticationComplete(NimBLEConnInfo& info) override {
    last_auth.store((info.isEncrypted() ? 1U : 0U) | (info.isBonded() ? 2U : 0U) |
                    (info.isAuthenticated() ? 4U : 0U));
    pairing_ends.fetch_add(1);
  }
};

ServerProxy server_proxy;  // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)

void CountBonds() {
  const int bonds = NimBLEDevice::getNumBonds();
  paired_phones.store(static_cast<uint8_t>(bonds < 0 ? 0 : bonds));
}

void AnswerPairing(bool accept) {
  NimBLEServer* server = NimBLEDevice::getServer();
  if (server == nullptr) {
    return;
  }
  const NimBLEConnInfo info = server->getPeerInfoByHandle(pairing_handle.load());
  NimBLEDevice::injectConfirmPasskey(info, accept);
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

// After every begin(). The library's handler is live for the few microseconds before this.
bool AttachRx() {
  NimBLEServer* server = NimBLEDevice::getServer();
  NimBLEService* service = server != nullptr ? server->getServiceByUUID(CS_SERVICE_UUID) : nullptr;
  NimBLECharacteristic* rx =
      service != nullptr ? service->getCharacteristic(CS_CHARACTERISTIC_UUID_RX) : nullptr;
  if (rx == nullptr) {
    return false;
  }
  rx->setCallbacks(&rx_callbacks);
  NimBLEDevice::setSecurityAuth(true, true, true);          // bonding, MITM, secure connections
  NimBLEDevice::setSecurityIOCap(BLE_HS_IO_DISPLAY_YESNO);  // the code on the dash, a knob yes/no
  server->setCallbacks(&server_proxy, false);
  CountBonds();
  return true;
}

void Send(ChronosAction action) {
  switch (action) {
    case ChronosAction::kMusicToggle:
      watch->musicControl(MUSIC_TOGGLE);
      break;
    case ChronosAction::kMusicNext:
      watch->musicControl(MUSIC_NEXT);
      break;
    case ChronosAction::kMusicPrevious:
      watch->musicControl(MUSIC_PREVIOUS);
      break;
    case ChronosAction::kPairAccept:
      AnswerPairing(true);
      break;
    case ChronosAction::kPairReject:
      AnswerPairing(false);
      break;
    case ChronosAction::kForgetPhones:
      ForgetPhones();
      break;
    case ChronosAction::kNone:
      break;
  }
}

// The library blocks for 200 ms after every notification it sends, so it runs here, never on
// the UI task.
void LinkTask(void* /*arg*/) {
  uint32_t bonds_counted_at = 0;
  for (;;) {
    if (want_paused.load() && !stopped.load()) {
      watch->stop(true);  // NimBLEDevice::deinit: frees the BLE stack's RAM for Wi-Fi
      connected.store(false);
      ble_up.store(false);
      stopped.store(true);
      ESP_LOGI(kTag, "BLE stopped for Wi-Fi");
    } else if (!want_paused.load() && stopped.load()) {
      watch->begin();
      if (!AttachRx()) {
        ESP_LOGE(kTag, "RX characteristic missing after restart");
      }
      stopped.store(false);
      ESP_LOGI(kTag, "BLE restarted");
    }
    uint8_t action = 0;
    if (stopped.load()) {
      while (xQueueReceive(action_queue, &action, 0) == pdTRUE) {
      }
    } else {
      // The app writes only the first 20 bytes of a longer packet until the MTU is raised, and
      // the phone may wait minutes to raise it, so nav (always longer) went missing. Either
      // side may start the exchange; Android answers with its maximum (docs/phase3-findings.md).
      if (mtu_wanted.exchange(false)) {
        const int rc = ble_gattc_exchange_mtu(mtu_handle.load(), nullptr, nullptr);
        if (rc != 0) {
          ESP_LOGW(kTag, "MTU exchange not started (rc %d)", rc);
        }
      }
      watch->loop();
      connected.store(watch->isConnected());
      if (pairing_ends.load() != bonds_counted_at) {
        bonds_counted_at = pairing_ends.load();
        CountBonds();
      }
      ble_up.store(true);
      while (xQueueReceive(action_queue, &action, 0) == pdTRUE) {
        Send(static_cast<ChronosAction>(action));
      }
    }
    vTaskDelay(pdMS_TO_TICKS(kLinkPeriodMs));
  }
}

}  // namespace

bool ChronosSource::Begin(const char* name) {
  rx_queue = xQueueCreateWithCaps(kRxQueueLength, sizeof(Write), MALLOC_CAP_SPIRAM);
  action_queue = xQueueCreate(kActionQueueLength, sizeof(uint8_t));
  // Zeroed PSRAM: the default constructor leaves some members uninitialised, and the object is
  // 11.7 KB. Not the (name, screen) constructor: it builds a second, temporary object on the
  // caller's stack (docs/phase3-findings.md).
  void* memory = heap_caps_calloc(1, sizeof(ChronosESP32), MALLOC_CAP_SPIRAM);
  if (rx_queue == nullptr || action_queue == nullptr || memory == nullptr) {
    ESP_LOGE(kTag, "out of memory");
    return false;
  }
  watch = new (memory) ChronosESP32();
  watch->setName(name);
  watch->setScreen(CS_466x466_143_CTF);  // the closest round screen; only the app shows it
  watch->setBattery(kDeviceBatteryPercent);
  watch->begin();
  if (!AttachRx()) {
    ESP_LOGE(kTag, "RX characteristic missing");
    return false;
  }
  ble_up.store(true);
  if (xTaskCreatePinnedToCore(LinkTask, "link", kStackBytes, nullptr, kPriority, nullptr, kCore) !=
      pdPASS) {
    ESP_LOGE(kTag, "link task failed to start");
    return false;
  }
  ESP_LOGI(kTag, "advertising as %s (Chronos)", name);
  return true;
}

void ChronosSource::SetBlePaused(bool paused) { want_paused.store(paused); }

bool ChronosSource::BleStopped() const { return stopped.load(); }

bool ChronosSource::BleUp() const { return ble_up.load(); }

void ChronosSource::Poll(uint32_t now_ms, core::AppModel& model) {
  if (!started_) {
    bridge_.Begin(model);
    started_ = true;
  }
  if (rx_queue == nullptr) {
    return;
  }
  // Writes first, then the link state: on a disconnect, data from before it can't revive
  // navigation after the bridge has cleared it.
  for (size_t i = 0; i < kMaxWritesPerPoll && xQueueReceive(rx_queue, &poll_write, 0) == pdTRUE;
       ++i) {
    ChronosMessage message;
    if (receiver_.Feed(poll_write.data.data(), poll_write.size, message)) {
      bridge_.OnMessage(message, now_ms, model);
    }
  }
  const bool now_connected = connected.load();
  if (now_connected != connected_) {
    connected_ = now_connected;
    bridge_.OnConnection(now_connected, model);
    ESP_LOGI(kTag, "phone %s", now_connected ? "connected" : "disconnected");
  }
  PollPairing(now_ms, model);
  bridge_.Tick(now_ms, model);

  const uint32_t dropped = receiver_.Dropped() + rx_overflow.load();
  if (dropped != dropped_logged_) {
    const ChronosDropInfo& drop = receiver_.LastDrop();
    ESP_LOGW(kTag,
             "dropped %u packet(s) so far (MTU %u); last: reason %u, command 0x%02X, write %u B, "
             "expected %u B, had %u B, queue overflows %u",
             static_cast<unsigned>(dropped), static_cast<unsigned>(mtu.load()),
             static_cast<unsigned>(drop.reason), drop.command,
             static_cast<unsigned>(drop.write_size), static_cast<unsigned>(drop.expected),
             static_cast<unsigned>(drop.received), static_cast<unsigned>(rx_overflow.load()));
    dropped_logged_ = dropped;
  }
}

void ChronosSource::PollPairing(uint32_t now_ms, core::AppModel& model) {
  if (pairing_requests.load() != pairing_requests_seen_) {
    pairing_requests_seen_ = pairing_requests.load();
    bridge_.OnPairingRequest(pairing_code.load(), now_ms, model);
    ESP_LOGI(kTag, "a phone asks to pair: confirm the code on the dash");  // never the code
  }
  if (pairing_ends.load() != pairing_ends_seen_) {
    pairing_ends_seen_ = pairing_ends.load();
    bridge_.OnPairingEnded(model);
    const uint32_t auth = last_auth.load();
    ESP_LOGI(kTag, "link security: encrypted %u, bonded %u, authenticated %u", auth & 1U,
             (auth >> 1U) & 1U, (auth >> 2U) & 1U);
  }
  if (paired_phones.load() != model.State().phone.paired_phones) {
    bridge_.OnPairedPhones(paired_phones.load(), model);
  }
  if (mtu.load() != mtu_logged_) {
    mtu_logged_ = mtu.load();
    ESP_LOGI(kTag, "MTU %u", static_cast<unsigned>(mtu_logged_));
  }
  if (rx_refused.load() != refused_logged_) {
    refused_logged_ = rx_refused.load();
    ESP_LOGW(kTag, "%u write(s) refused from a phone that isn't paired",
             static_cast<unsigned>(refused_logged_));
  }
}

void ChronosSource::OnCommand(const core::Command& command, uint32_t now_ms,
                              core::AppModel& model) {
  const ChronosAction action = bridge_.OnCommand(command, now_ms, model);
  if (action != ChronosAction::kNone && action_queue != nullptr) {
    const auto value = static_cast<uint8_t>(action);
    xQueueSend(action_queue, &value, 0);
  }
}

}  // namespace sarathi::link

#endif  // SARATHI_BACKEND_CHRONOS
