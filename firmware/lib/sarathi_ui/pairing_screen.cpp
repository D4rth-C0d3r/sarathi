#include "pairing_screen.h"

#include "layout.h"
#include "static_label.h"
#include "theme_binding.h"

namespace sarathi::ui::pairing_screen {
namespace {

// Laid out like the call screen: two full-width buttons, stacked, so "Cancel" fits at the
// essential size.
constexpr int32_t kTitleY = -150;
constexpr int32_t kCodeY = -90;
constexpr int32_t kHintY = -35;
constexpr int32_t kPairY = 40;
constexpr int32_t kCancelY = 135;
constexpr int32_t kButtonWidth = 260;
constexpr int32_t kButtonHeight = 80;
constexpr uint32_t kCodeHalf = 1000;  // "708 361": two groups of three digits

// NOLINTBEGIN(cppcoreguidelines-avoid-non-const-global-variables)
core::AppModel* app = nullptr;
lv_group_t* focus_group = nullptr;
StaticLabel<24> title;
StaticLabel<12> code;
StaticLabel<48> hint;
lv_obj_t* cancel_button = nullptr;
// NOLINTEND(cppcoreguidelines-avoid-non-const-global-variables)

void OnPair(lv_event_t* /*event*/) { app->AcceptPairing(); }
void OnCancel(lv_event_t* /*event*/) { app->RejectPairing(); }

}  // namespace

lv_obj_t* Create(lv_group_t* group, core::AppModel& model) {
  app = &model;
  focus_group = group;
  lv_obj_t* screen = theme::MakeScreen();
  title.Create(screen, theme::Text::kEssential, kTitleY, layout::kContentWidth);
  title.Set("%s", "Pair phone?");
  code.Create(screen, theme::Text::kPrimary, kCodeY, layout::kContentWidth);
  hint.Create(screen, theme::Text::kStoppedOnly, kHintY, layout::kContentWidth);
  hint.Set("%s", "Same code on phone?");

  lv_obj_t* pair = theme::MakeButton(screen, "Pair", theme::Fill::kAccept,
                                     core::FontRole::kEssential, kButtonWidth, kButtonHeight);
  lv_obj_align(pair, LV_ALIGN_CENTER, 0, kPairY);
  lv_obj_add_event_cb(pair, OnPair, LV_EVENT_CLICKED, nullptr);
  cancel_button = theme::MakeButton(screen, "Cancel", theme::Fill::kNeutral,
                                    core::FontRole::kEssential, kButtonWidth, kButtonHeight);
  lv_obj_align(cancel_button, LV_ALIGN_CENTER, 0, kCancelY);
  lv_obj_add_event_cb(cancel_button, OnCancel, LV_EVENT_CLICKED, nullptr);
  lv_group_add_obj(group, pair);
  lv_group_add_obj(group, cancel_button);
  Reset();
  return screen;
}

void Reset() { lv_group_focus_obj(cancel_button); }

void Update(const core::AppState& state) {
  const uint32_t c = state.pairing.code;
  code.Set("%03u %03u", static_cast<unsigned>(c / kCodeHalf), static_cast<unsigned>(c % kCodeHalf));
}

}  // namespace sarathi::ui::pairing_screen
