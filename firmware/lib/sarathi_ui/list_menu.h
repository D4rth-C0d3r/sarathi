// Semantic component: a knob-driven vertical menu. Rows are actions (click runs them) or
// values (click starts adjusting, rotation changes the value, click again confirms; uses
// LVGL's group edit mode). All rows are created once; nothing allocates afterwards.

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include <lvgl.h>

namespace sarathi::ui {

class ListMenu {
 public:
  using ActionFn = void (*)(void* context);
  using GetFn = int (*)(void* context);
  using SetFn = void (*)(void* context, int value);

  static constexpr size_t kMaxRows = 12;

  // `max_height` > 0 makes a longer list scroll, keeping the focused row centred.
  void Create(lv_obj_t* parent, int32_t y_offset, int32_t width, int32_t max_height = 0);

  // Both return the row index.
  size_t AddAction(const char* text, ActionFn on_click, void* context);
  size_t AddValue(const char* label, const char* unit, int min, int max, int step, GetFn get,
                  SetFn set, void* context);

  // Replaces an action row's text (for example "Theme: Classic").
  void SetText(size_t row, const char* text);

  // Re-reads value rows from their getters.
  void Refresh();

  // Rows join `group` only while the menu is shown, so a hidden menu can't take focus.
  void Show(lv_group_t* group);
  void Hide();

 private:
  enum class Kind : uint8_t { kAction, kValue };

  struct Row {
    ListMenu* menu = nullptr;
    Kind kind = Kind::kAction;
    lv_obj_t* button = nullptr;
    lv_obj_t* label = nullptr;
    std::array<char, 40> text{};
    const char* name = "";
    const char* unit = "";
    int min = 0;
    int max = 0;
    int step = 1;
    ActionFn on_click = nullptr;
    GetFn get = nullptr;
    SetFn set = nullptr;
    void* context = nullptr;
  };

  static void OnClicked(lv_event_t* event);
  static void OnKey(lv_event_t* event);

  Row& NewRow(Kind kind);
  void WriteValueText(Row& row) const;
  void StopEditing();

  lv_obj_t* container_ = nullptr;
  lv_group_t* group_ = nullptr;
  std::array<Row, kMaxRows> rows_{};
  size_t count_ = 0;
};

}  // namespace sarathi::ui
