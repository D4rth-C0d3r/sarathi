// Showing and hiding objects. lv_obj_add/remove_flag(HIDDEN) invalidates the object even when
// nothing changes, which would force a redraw on every update; this touches the flag only
// when the visibility actually changes.

#pragma once

#include <lvgl.h>

namespace sarathi::ui {

inline void SetVisible(lv_obj_t* obj, bool visible) {
  if (visible == lv_obj_has_flag(obj, LV_OBJ_FLAG_HIDDEN)) {
    if (visible) {
      lv_obj_remove_flag(obj, LV_OBJ_FLAG_HIDDEN);
    } else {
      lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
    }
  }
}

}  // namespace sarathi::ui
