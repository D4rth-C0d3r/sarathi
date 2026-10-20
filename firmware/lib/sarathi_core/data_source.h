// Where app data comes from: the phone over BLE later (Phase 4), the demo ride now.

#pragma once

#include <cstdint>

#include "app_model.h"

namespace sarathi::core {

class DataSource {
 public:
  DataSource() = default;
  DataSource(const DataSource&) = delete;
  DataSource& operator=(const DataSource&) = delete;
  DataSource(DataSource&&) = delete;
  DataSource& operator=(DataSource&&) = delete;
  virtual ~DataSource() = default;

  // Push new data into the model. Call from the UI task, every loop.
  virtual void Poll(uint32_t now_ms, AppModel& model) = 0;

  // A rider action for the phone (accept a call, skip a track, ...).
  virtual void OnCommand(const Command& command, uint32_t now_ms, AppModel& model) = 0;
};

}  // namespace sarathi::core
