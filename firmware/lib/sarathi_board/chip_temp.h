// The ESP32-S3's internal temperature sensor (the die, not the air), for the heat cap on the
// backlight (core/ride_health.h). ESP-IDF's driver directly: Arduino's temperatureRead() sets the
// sensor up for 10-50 °C and fails above that, just when the reading matters.

#pragma once

namespace sarathi::board {

class ChipTemp {
 public:
  // Installs and starts the sensor. Returns false if it can't; Read() then fails.
  bool Begin();

  // Returns false if the sensor isn't running or the reading failed.
  bool Read(float* celsius);

 private:
  void* handle_ = nullptr;  // temperature_sensor_handle_t, kept out of this header
};

}  // namespace sarathi::board
