// Backlight PWM on GPIO 6. Brightness is in percent, clamped to 5-100 %.

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
  int percent_ = 0;
};

}  // namespace sarathi::board
