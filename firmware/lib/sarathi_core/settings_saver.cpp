#include "settings_saver.h"

namespace sarathi::core {

void SettingsSaver::MarkSaved(uint32_t revision) {
  saved_revision_ = revision;
  seen_revision_ = revision;
  pending_ = false;
}

bool SettingsSaver::ShouldSave(uint32_t revision, uint32_t now_ms) {
  if (revision != seen_revision_) {
    seen_revision_ = revision;
    changed_ms_ = now_ms;
    pending_ = revision != saved_revision_;
  }
  if (pending_ && now_ms - changed_ms_ >= delay_ms_) {
    saved_revision_ = seen_revision_;
    pending_ = false;
    return true;
  }
  return false;
}

void SettingsSaver::SaveFailed(uint32_t now_ms) {
  pending_ = true;
  changed_ms_ = now_ms;
}

}  // namespace sarathi::core
