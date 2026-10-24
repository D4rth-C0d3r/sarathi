#include "call_screen.h"

#include <array>

#include "format.h"
#include "layout.h"
#include "static_label.h"
#include "theme_binding.h"

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
lv_obj_t* dismiss_button = nullptr;  // neutral: hiding the call isn't a rejection
core::CallPhase shown_phase = core::CallPhase::kIdle;
core::Capabilities shown_caps{};
// NOLINTEND(cppcoreguidelines-avoid-non-const-global-variables)

void OnAccept(lv_event_t* /*event*/) { app->AcceptCall(); }
void OnReject(lv_event_t* /*event*/) { app->RejectCall(); }
void OnDismiss(lv_event_t* /*event*/) { app->DismissCall(); }

// Only the actions the data source supports (ADR-0005). Accept is only there while ringing;
// Reject becomes End once the call is active. Without call control (Chronos) a single
// Dismiss hides the call on the dash; the rider answers on the intercom.
void ShowPhase(core::CallPhase phase, const core::Capabilities& caps) {
  lv_group_remove_all_objs(focus_group);
  const bool ringing = phase == core::CallPhase::kRinging;
  const bool accept = ringing && caps.call_accept;
  if (accept) {
    lv_obj_remove_flag(accept_button, LV_OBJ_FLAG_HIDDEN);
    lv_group_add_obj(focus_group, accept_button);
  } else {
    lv_obj_add_flag(accept_button, LV_OBJ_FLAG_HIDDEN);
  }
  lv_obj_t* second = caps.call_reject ? reject_button : dismiss_button;
  lv_obj_add_flag(caps.call_reject ? dismiss_button : reject_button, LV_OBJ_FLAG_HIDDEN);
  lv_obj_remove_flag(second, LV_OBJ_FLAG_HIDDEN);
  lv_label_set_text_static(reject_label, ringing ? "Reject" : "End");
  lv_obj_align(second, LV_ALIGN_CENTER, 0, accept ? 135 : 70);
  lv_group_add_obj(focus_group, second);
  lv_group_focus_obj(accept ? accept_button : second);
  shown_phase = phase;
  shown_caps = caps;
}

bool SameCallActions(const core::Capabilities& a, const core::Capabilities& b) {
  return a.call_accept == b.call_accept && a.call_reject == b.call_reject;
}

}  // namespace

lv_obj_t* Create(lv_group_t* group, core::AppModel& model) {
  app = &model;
  focus_group = group;
  lv_obj_t* screen = theme::MakeScreen();
  header.Create(screen, theme::Text::kSupporting, -140, layout::kContentWidth);
  caller.Create(screen, theme::Text::kEssential, -70, layout::kContentWidth);

  accept_button = theme::MakeButton(screen, "Accept", theme::Fill::kAccept,
                                    core::FontRole::kEssential, kButtonWidth, kButtonHeight);
  lv_obj_align(accept_button, LV_ALIGN_CENTER, 0, 40);
  lv_obj_add_event_cb(accept_button, OnAccept, LV_EVENT_CLICKED, nullptr);

  reject_button = theme::MakeButton(screen, "Reject", theme::Fill::kReject,
                                    core::FontRole::kEssential, kButtonWidth, kButtonHeight);
  reject_label = lv_obj_get_child(reject_button, 0);
  lv_obj_add_event_cb(reject_button, OnReject, LV_EVENT_CLICKED, nullptr);

  dismiss_button = theme::MakeButton(screen, "Dismiss", theme::Fill::kNeutral,
                                     core::FontRole::kEssential, kButtonWidth, kButtonHeight);
  lv_obj_add_event_cb(dismiss_button, OnDismiss, LV_EVENT_CLICKED, nullptr);

  ShowPhase(core::CallPhase::kRinging, shown_caps);
  return screen;
}

void Update(const core::AppState& state, uint32_t now_ms) {
  const core::CallState& call = state.call;
  if (call.phase != core::CallPhase::kIdle &&
      (call.phase != shown_phase || !SameCallActions(state.caps, shown_caps))) {
    ShowPhase(call.phase, state.caps);
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
