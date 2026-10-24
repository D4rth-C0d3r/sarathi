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
    case ChronosAction::kNone:
      break;
  }
}

// The library blocks for 200 ms after every notification it sends, so it runs here, never on
// the UI task.
void LinkTask(void* /*arg*/) {
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
      watch->loop();
      connected.store(watch->isConnected());
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
