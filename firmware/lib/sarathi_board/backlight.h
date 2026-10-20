// Backlight PWM on GPIO 6. Brightness is in percent: 0 turns it off (screen timeout); anything
// else is clamped to 5-100 % (at 0 % the screen is unreadable, so users can't set it).

#pragma once

namespace sarathi::board {

class Backlight {
 public:
  // Attaches the PWM and sets the starting brightness. Returns false if LEDC attach fails.
  bool Begin(int percent);

  void SetPercent(int percent);

  // The applied (clamped) brightness.
  [[nodiscard]] int Percent() const { return percent_; }

 private:
  int percent_ = -1;  // nothing written yet
};

}  // namespace sarathi::board
