#include "list_menu.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "theme_binding.h"

namespace sarathi::ui {
namespace {

constexpr int32_t kRowGap = 12;

}  // namespace

void ListMenu::Create(lv_obj_t* parent, int32_t y_offset, int32_t width) {
  container_ = lv_obj_create(parent);
  lv_obj_remove_style_all(container_);
  lv_obj_set_width(container_, width);
  lv_obj_set_height(container_, LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(container_, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row(container_, kRowGap, 0);
  lv_obj_set_style_pad_all(container_, 8, 0);  // room for the focus ring
  lv_obj_remove_flag(container_, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_align(container_, LV_ALIGN_CENTER, 0, y_offset);
}

ListMenu::Row& ListMenu::NewRow(Kind kind) {
  Row& row = rows_.at(std::min(count_, kMaxRows - 1));
  if (count_ < kMaxRows) {
    ++count_;
  }
  row.menu = this;
  row.kind = kind;
  row.button = lv_button_create(container_);
  lv_obj_set_width(row.button, LV_PCT(100));
  lv_obj_add_style(row.button, theme::ListRowStyle(), 0);
  lv_obj_add_style(row.button, theme::FocusStyle(), LV_STATE_FOCUSED);
  lv_obj_add_style(row.button, theme::EditStyle(), LV_STATE_EDITED);
  lv_obj_add_event_cb(row.button, OnClicked, LV_EVENT_CLICKED, &row);
  lv_obj_add_event_cb(row.button, OnKey, LV_EVENT_KEY, &row);
  row.label = lv_label_create(row.button);
  lv_obj_set_width(row.label, LV_PCT(100));
  lv_label_set_long_mode(row.label, LV_LABEL_LONG_DOT);  // never spill out of the row
  lv_obj_set_style_text_align(row.label, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_center(row.label);
  row.text[0] = '\0';
  lv_label_set_text_static(row.label, row.text.data());
  return row;
}

size_t ListMenu::AddAction(const char* text, ActionFn on_click, void* context) {
  Row& row = NewRow(Kind::kAction);
  row.on_click = on_click;
  row.context = context;
  SetText(static_cast<size_t>(&row - rows_.data()), text);
  return static_cast<size_t>(&row - rows_.data());
}

size_t ListMenu::AddValue(const char* label, const char* unit, int min, int max, int step,
                          GetFn get, SetFn set, void* context) {
  Row& row = NewRow(Kind::kValue);
  row.name = label;
  row.unit = unit;
  row.min = min;
  row.max = max;
  row.step = step;
  row.get = get;
  row.set = set;
  row.context = context;
  WriteValueText(row);
  return static_cast<size_t>(&row - rows_.data());
}

void ListMenu::SetText(size_t row, const char* text) {
  if (row >= count_) {
    return;
  }
  Row& r = rows_.at(row);
  if (std::strncmp(r.text.data(), text, r.text.size()) != 0) {
    std::snprintf(r.text.data(), r.text.size(), "%s", text);
    lv_label_set_text_static(r.label, r.text.data());
  }
}

void ListMenu::WriteValueText(Row& row) const {
  std::array<char, 40> next{};
  std::snprintf(next.data(), next.size(), "%s  %d%s", row.name, row.get(row.context), row.unit);
  if (std::strcmp(next.data(), row.text.data()) != 0) {
    row.text = next;
    lv_label_set_text_static(row.label, row.text.data());
  }
}

void ListMenu::Refresh() {
  for (size_t i = 0; i < count_; ++i) {
    if (rows_.at(i).kind == Kind::kValue) {
      WriteValueText(rows_.at(i));
    }
  }
}

void ListMenu::Show(lv_group_t* group) {
  group_ = group;
  lv_obj_remove_flag(container_, LV_OBJ_FLAG_HIDDEN);
  for (size_t i = 0; i < count_; ++i) {
    lv_group_add_obj(group, rows_.at(i).button);
  }
  if (count_ > 0) {
    lv_group_focus_obj(rows_.at(0).button);
  }
}

void ListMenu::Hide() {
  StopEditing();
  lv_obj_add_flag(container_, LV_OBJ_FLAG_HIDDEN);
  for (size_t i = 0; i < count_; ++i) {
    lv_group_remove_obj(rows_.at(i).button);
  }
  group_ = nullptr;
}

void ListMenu::StopEditing() {
  if (group_ != nullptr && lv_group_get_editing(group_)) {
    lv_group_set_editing(group_, false);
  }
  for (size_t i = 0; i < count_; ++i) {
    lv_obj_remove_state(rows_.at(i).button, LV_STATE_EDITED);
  }
}

void ListMenu::OnClicked(lv_event_t* event) {
  auto* row = static_cast<Row*>(lv_event_get_user_data(event));
  ListMenu* menu = row->menu;
  if (row->kind == Kind::kAction) {
    if (row->on_click != nullptr) {
      row->on_click(row->context);
    }
    return;
  }
  // Value row: a click toggles adjusting. While adjusting, rotation arrives as LEFT/RIGHT keys.
  if (menu->group_ == nullptr) {
    return;
  }
  if (lv_group_get_editing(menu->group_)) {
    menu->StopEditing();
  } else {
    lv_group_set_editing(menu->group_, true);
    lv_obj_add_state(row->button, LV_STATE_EDITED);
  }
}

void ListMenu::OnKey(lv_event_t* event) {
  auto* row = static_cast<Row*>(lv_event_get_user_data(event));
  if (row->kind != Kind::kValue) {
    return;
  }
  const uint32_t key = lv_event_get_key(event);
  int delta = 0;
  if (key == LV_KEY_RIGHT || key == LV_KEY_UP) {
    delta = row->step;
  } else if (key == LV_KEY_LEFT || key == LV_KEY_DOWN) {
    delta = -row->step;
  }
  if (delta != 0) {
    row->set(row->context, std::clamp(row->get(row->context) + delta, row->min, row->max));
    row->menu->WriteValueText(*row);
  }
}

}  // namespace sarathi::ui
