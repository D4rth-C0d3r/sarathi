// Wi-Fi setup screen (ADR-0004 §2): tells the rider which network to join on their phone and
// its password, then shows how the join went. Opened from Settings. Click closes a finished
// setup; a long press cancels.

#pragma once

#include <lvgl.h>

#include "wifi_setup.h"

namespace sarathi::ui::wifi_setup_screen {

lv_obj_t* Create();
void Update(const core::WifiSetup& setup);

}  // namespace sarathi::ui::wifi_setup_screen
