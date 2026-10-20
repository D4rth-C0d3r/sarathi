// Debounce and click detection for the knob button. Pure logic; time is passed in.
//
// Behaviour follows Elecrow's pollEncoderButton() (vendor/crowpanel-2.1/example/Arduino/
// RotaryScreen_2_1/RotaryScreen_2_1.ino): a level must hold for the debounce time to count,
// and a second completed press within the double-click window makes a double click.
// Optionally, holding the button for the long-press time makes a long press (and that press
// doesn't also count as a click).

#pragma once

#include <cstdint>

namespace sarathi::core {

enum class ButtonGesture : uint8_t { kNone, kClick, kDoubleClick, kLongPress };

struct ButtonUpdate {
  bool pressed;           // debounced state after this update
  bool changed;           // debounced state changed in this update
  ButtonGesture gesture;  // gesture completed in this update, if any
};

class ButtonDebouncer {
 public:
  // `long_press_ms` = 0 disables long presses.
  ButtonDebouncer(uint32_t debounce_ms, uint32_t double_click_ms, uint32_t long_press_ms = 0);

  // Call regularly (every few ms) with the raw level, true = pressed.
  ButtonUpdate Update(bool raw_pressed, uint32_t now_ms);

  [[nodiscard]] bool Pressed() const { return stable_; }

 private:
  uint32_t debounce_ms_;
  uint32_t double_click_ms_;
  uint32_t long_press_ms_;
  bool raw_ = false;
  uint32_t pressed_since_ms_ = 0;
  bool long_fired_ = false;
  bool stable_ = false;
  uint32_t raw_changed_ms_ = 0;
  uint8_t press_count_ = 0;
  uint32_t click_deadline_ms_ = 0;
};

}  // namespace sarathi::core
