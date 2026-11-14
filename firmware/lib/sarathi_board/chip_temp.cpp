#include "chip_temp.h"

#include <driver/temperature_sensor.h>
#include <esp32-hal-log.h>

namespace sarathi::board {
namespace {

constexpr const char* kTag = "chiptemp";

// One of the sensor's measuring ranges (ESP-IDF "Temperature Sensor", ESP32-S3: 20-100 °C,
// error under 2 °C): the band the heat cap works in. Below 20 °C the reading is less accurate,
// which doesn't matter here.
constexpr int kRangeMinC = 20;
constexpr int kRangeMaxC = 100;

}  // namespace

bool ChipTemp::Begin() {
  temperature_sensor_config_t config = TEMPERATURE_SENSOR_CONFIG_DEFAULT(kRangeMinC, kRangeMaxC);
  temperature_sensor_handle_t handle = nullptr;
  if (temperature_sensor_install(&config, &handle) != ESP_OK) {
    ESP_LOGE(kTag, "sensor install failed");
    return false;
  }
  if (temperature_sensor_enable(handle) != ESP_OK) {
    temperature_sensor_uninstall(handle);
    ESP_LOGE(kTag, "sensor enable failed");
    return false;
  }
  handle_ = handle;
  return true;
}

bool ChipTemp::Read(float* celsius) {
  if (handle_ == nullptr) {
    return false;
  }
  return temperature_sensor_get_celsius(static_cast<temperature_sensor_handle_t>(handle_),
                                        celsius) == ESP_OK;
}

}  // namespace sarathi::board
