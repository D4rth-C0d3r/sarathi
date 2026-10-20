#include "call_screen.h"

#include <array>

#include "format.h"
#include "placeholder_style.h"
#include "static_label.h"

namespace sarathi::ui::call_screen {
namespace {

constexpr int32_t kButtonWidth = 260;
constexpr int32_t kButtonHeight = 80;

// NOLINTBEGIN(cppcoreguidelines-avoid-non-const-global-variables)
core::AppModel* app = nullptr;
lv_group_t* focus_group = nullptr;
StaticLabel<32> header;
StaticLabel<40> caller;
lv_obj_t* accept_button = nullptr;
lv_obj_t* reject_button = nullptr;
lv_obj_t* reject_label = nullptr;
core::CallPhase shown_phase = core::CallPhase::kIdle;
// NOLINTEND(cppcoreguidelines-avoid-non-const-global-variables)

void OnAccept(lv_event_t* /*event*/) { app->AcceptCall(); }
void OnReject(lv_event_t* /*event*/) { app->RejectCall(); }

// Accept is only there while ringing; Reject becomes End once the call is active.
void ShowPhase(core::CallPhase phase) {
  lv_group_remove_all_objs(focus_group);
  if (phase == core::CallPhase::kRinging) {
    lv_obj_remove_flag(accept_button, LV_OBJ_FLAG_HIDDEN);
    lv_obj_align(reject_button, LV_ALIGN_CENTER, 0, 135);
    lv_label_set_text_static(reject_label, "Reject");
    lv_group_add_obj(focus_group, accept_button);
    lv_group_add_obj(focus_group, reject_button);
    lv_group_focus_obj(accept_button);
  } else {
    lv_obj_add_flag(accept_button, LV_OBJ_FLAG_HIDDEN);
    lv_obj_align(reject_button, LV_ALIGN_CENTER, 0, 70);
    lv_label_set_text_static(reject_label, "End");
    lv_group_add_obj(focus_group, reject_button);
    lv_group_focus_obj(reject_button);
  }
  shown_phase = phase;
}

}  // namespace

lv_obj_t* Create(lv_group_t* group, core::AppModel& model) {
  app = &model;
  focus_group = group;
  lv_obj_t* screen = style::MakeScreen();
  header.Create(screen, style::Supporting(), style::kTextSupporting, -140, style::kContentWidth);
  caller.Create(screen, style::Essential(), style::kText, -70, style::kContentWidth);

  accept_button = style::MakeButton(screen, "Accept", style::Essential(), style::kAcceptFill,
                                    kButtonWidth, kButtonHeight);
  lv_obj_align(accept_button, LV_ALIGN_CENTER, 0, 40);
  lv_obj_add_event_cb(accept_button, OnAccept, LV_EVENT_CLICKED, nullptr);

  reject_button = style::MakeButton(screen, "Reject", style::Essential(), style::kRejectFill,
                                    kButtonWidth, kButtonHeight);
  reject_label = lv_obj_get_child(reject_button, 0);
  lv_obj_add_event_cb(reject_button, OnReject, LV_EVENT_CLICKED, nullptr);

  ShowPhase(core::CallPhase::kRinging);
  return screen;
}

void Update(const core::AppState& state, uint32_t now_ms) {
  const core::CallState& call = state.call;
  if (call.phase != core::CallPhase::kIdle && call.phase != shown_phase) {
    ShowPhase(call.phase);
  }
  if (call.phase == core::CallPhase::kActive) {
    std::array<char, 16> clock{};
    core::FormatClock((now_ms - call.phase_since_ms) / 1000, clock.data(), clock.size());
    header.Set("On call %s", clock.data());
  } else {
    header.Set("%s", "Incoming call");
  }
  caller.Set("%s", call.caller.Empty() ? "Unknown" : call.caller.CStr());
}

}  // namespace sarathi::ui::call_screen
