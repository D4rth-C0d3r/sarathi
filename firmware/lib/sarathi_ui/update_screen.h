// Software update screen (ADR-0004 §6): shows core::OtaUpdate's phase. Opened from Settings
// (so only when stopped). Click confirms an offered update or closes a finished check; a long
// press cancels until installing starts. Static while downloading: the progress redraws at
// most once per percent, because flash writes can disturb the panel.

#pragma once

#include <lvgl.h>

#include "ota_update.h"
#include "wifi_setup.h"

namespace sarathi::ui::update_screen {

lv_obj_t* Create();
void Update(const core::OtaUpdate& ota, const core::WifiSetup& wifi);

}  // namespace sarathi::ui::update_screen
