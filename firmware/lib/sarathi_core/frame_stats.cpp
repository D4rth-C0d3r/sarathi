#include "frame_stats.h"

#include <algorithm>

namespace sarathi::core {

void FrameStats::Add(uint32_t duration_us) {
  ++count_;
  total_us_ += duration_us;
  max_us_ = std::max(max_us_, duration_us);
}

FrameStats::Summary FrameStats::TakeSummary() {
  Summary s;
  s.count = count_;
  s.avg_us = count_ > 0 ? static_cast<uint32_t>(total_us_ / count_) : 0;
  s.max_us = max_us_;
  count_ = 0;
  total_us_ = 0;
  max_us_ = 0;
  return s;
}

}  // namespace sarathi::core
