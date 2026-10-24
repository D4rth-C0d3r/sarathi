// Pairing: a phone asks to bond, and the rider compares its 6-digit code with the phone's
// (CLAUDE.md, Security: bonding is confirmed by a knob press). Shown by the screen manager
// while a request is open, like a call. Focus starts on Cancel, so pairing takes a deliberate
// turn and click: a stray press, or a stranger's request, can't pair.

#pragma once

#include <lvgl.h>

#include "app_model.h"

namespace sarathi::ui::pairing_screen {

lv_obj_t* Create(lv_group_t* group, core::AppModel& model);
void Update(const core::AppState& state);

// Focus back on Cancel; call before showing the screen.
void Reset();

}  // namespace sarathi::ui::pairing_screen
