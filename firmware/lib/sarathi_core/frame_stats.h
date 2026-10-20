// Accumulates durations (frame time, flush time) and summarises them per reporting period.
// Pure logic; the platform supplies the microsecond timestamps.

#pragma once

#include <cstdint>

namespace sarathi::core {

class FrameStats {
 public:
  struct Summary {
    uint32_t count = 0;
    uint32_t avg_us = 0;
    uint32_t max_us = 0;
  };

  void Add(uint32_t duration_us);

  // Returns the summary since the last call and starts a new period.
  Summary TakeSummary();

 private:
  uint32_t count_ = 0;
  uint64_t total_us_ = 0;
  uint32_t max_us_ = 0;
};

}  // namespace sarathi::core
