// Debounces settings writes: save only once the settings have stayed unchanged for a while,
// so a burst of changes (turning brightness 80 -> 60) is one flash write. Flash writes can make
// the RGB panel flicker (CLAUDE.md), so they should be rare. Pure logic.

#pragma once

#include <cstdint>

namespace sarathi::core {

constexpr uint32_t kSettingsSaveDelayMs = 2000;

class SettingsSaver {
 public:
  explicit SettingsSaver(uint32_t delay_ms = kSettingsSaveDelayMs) : delay_ms_(delay_ms) {}

  // The revision that is already in storage (after loading at boot).
  void MarkSaved(uint32_t revision);

  // Call every loop with AppModel::SettingsRevision(). Returns true when it is time to write;
  // the saver then treats that revision as saved.
  bool ShouldSave(uint32_t revision, uint32_t now_ms);

  // The write failed: try again after another delay.
  void SaveFailed(uint32_t now_ms);

 private:
  uint32_t delay_ms_;
  uint32_t saved_revision_ = 0;
  uint32_t seen_revision_ = 0;
  uint32_t changed_ms_ = 0;
  bool pending_ = false;
};

}  // namespace sarathi::core
