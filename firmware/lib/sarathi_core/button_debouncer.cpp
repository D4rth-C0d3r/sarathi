#include "button_debouncer.h"

namespace sarathi::core {
namespace {

// True once `now` has reached `deadline`, correct across millis() wrap-around.
bool Reached(uint32_t now, uint32_t deadline) { return static_cast<int32_t>(now - deadline) >= 0; }

}  // namespace

ButtonDebouncer::ButtonDebouncer(uint32_t debounce_ms, uint32_t double_click_ms)
    : debounce_ms_(debounce_ms), double_click_ms_(double_click_ms) {}

ButtonUpdate ButtonDebouncer::Update(bool raw_pressed, uint32_t now_ms) {
  ButtonUpdate result{stable_, false, ButtonGesture::kNone};

  if (raw_pressed != raw_) {
    raw_ = raw_pressed;
    raw_changed_ms_ = now_ms;
  }

  if (raw_ != stable_ && now_ms - raw_changed_ms_ >= debounce_ms_) {
    stable_ = raw_;
    result.pressed = stable_;
    result.changed = true;
    if (!stable_) {  // a press completed
      ++press_count_;
      click_deadline_ms_ = now_ms + double_click_ms_;
    }
  }

  if (press_count_ >= 2) {
    press_count_ = 0;
    result.gesture = ButtonGesture::kDoubleClick;
  } else if (press_count_ == 1 && !stable_ && Reached(now_ms, click_deadline_ms_)) {
    press_count_ = 0;
    result.gesture = ButtonGesture::kClick;
  }
  return result;
}

}  // namespace sarathi::core
