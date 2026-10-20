#include "input.h"

#include <algorithm>
#include <cstdint>

namespace sarathi::ui::input {
namespace {

enum class ClickPhase : uint8_t { kIdle, kPress, kRelease };

// NOLINTBEGIN(cppcoreguidelines-avoid-non-const-global-variables)
int pending_steps = 0;
ClickPhase click = ClickPhase::kIdle;
// NOLINTEND(cppcoreguidelines-avoid-non-const-global-variables)

void ReadEncoder(lv_indev_t* /*indev*/, lv_indev_data_t* data) {
  const int steps = std::clamp(pending_steps, INT16_MIN, INT16_MAX);
  data->enc_diff = static_cast<int16_t>(steps);
  pending_steps -= steps;

  // A click is reported as pressed on one read and released on the next.
  switch (click) {
    case ClickPhase::kPress:
      data->state = LV_INDEV_STATE_PRESSED;
      click = ClickPhase::kRelease;
      break;
    case ClickPhase::kRelease:
      data->state = LV_INDEV_STATE_RELEASED;
      click = ClickPhase::kIdle;
      break;
    case ClickPhase::kIdle:
      data->state = LV_INDEV_STATE_RELEASED;
      break;
  }
}

}  // namespace

lv_indev_t* Init() {
  lv_indev_t* encoder = lv_indev_create();
  lv_indev_set_type(encoder, LV_INDEV_TYPE_ENCODER);
  lv_indev_set_read_cb(encoder, ReadEncoder);
  return encoder;
}

void AddSteps(int steps) { pending_steps += steps; }

void Click() {
  if (click == ClickPhase::kIdle) {
    click = ClickPhase::kPress;
  }
}

}  // namespace sarathi::ui::input
