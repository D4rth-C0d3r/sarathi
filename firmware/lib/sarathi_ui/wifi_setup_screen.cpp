#include "wifi_setup_screen.h"

#include "layout.h"
#include "static_label.h"
#include "theme_binding.h"

namespace sarathi::ui::wifi_setup_screen {
namespace {

constexpr int32_t kTitleY = -165;
constexpr int32_t kJoinY = -112;
constexpr int32_t kApNameY = -68;
constexpr int32_t kPasswordLabelY = -12;
constexpr int32_t kPasswordY = 40;
constexpr int32_t kStatusY = 112;
constexpr int32_t kHintY = 168;
constexpr int32_t kStatusWidth = 360;

// NOLINTBEGIN(cppcoreguidelines-avoid-non-const-global-variables)
StaticLabel<24> title;
StaticLabel<32> join_line;
StaticLabel<24> ap_name;
StaticLabel<24> password_label;
StaticLabel<16> password;
StaticLabel<96> status;
StaticLabel<32> hint;
uint32_t shown_revision = UINT32_MAX;
// NOLINTEND(cppcoreguidelines-avoid-non-const-global-variables)

void ShowCredentials(bool show) {
  for (lv_obj_t* obj : {join_line.Obj(), ap_name.Obj(), password_label.Obj(), password.Obj()}) {
    if (show) {
      lv_obj_remove_flag(obj, LV_OBJ_FLAG_HIDDEN);
    } else {
      lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
    }
  }
}

}  // namespace

lv_obj_t* Create() {
  lv_obj_t* screen = theme::MakeScreen();
  title.Create(screen, theme::Text::kStoppedOnly, kTitleY);
  title.Set("%s", "Wi-Fi setup");
  join_line.Create(screen, theme::Text::kStoppedOnly, kJoinY, layout::kContentWidth);
  join_line.Set("%s", "On your phone, join");
  ap_name.Create(screen, theme::Text::kEssential, kApNameY, layout::kContentWidth);
  ap_name.Set("%s", core::kSetupApName);
  password_label.Create(screen, theme::Text::kStoppedOnly, kPasswordLabelY);
  password_label.Set("%s", "Password");
  password.Create(screen, theme::Text::kPrimary, kPasswordY, layout::kContentWidth);
  status.Create(screen, theme::Text::kStoppedOnly, kStatusY, kStatusWidth);
  lv_label_set_long_mode(status.Obj(), LV_LABEL_LONG_WRAP);
  hint.Create(screen, theme::Text::kStoppedOnly, kHintY, layout::kContentWidth);
  return screen;
}

void Update(const core::WifiSetup& setup) {
  if (setup.Revision() == shown_revision) {
    return;
  }
  shown_revision = setup.Revision();
  const std::string_view ssid = setup.JoiningSsid();
  const auto ssid_len = static_cast<int>(ssid.size());
  password.Set("%.*s", static_cast<int>(setup.Password().size()), setup.Password().data());
  switch (setup.Phase()) {
    case core::WifiSetupPhase::kIdle:
    case core::WifiSetupPhase::kStarting:
      ShowCredentials(true);
      status.Set("%s", "Starting...");
      hint.Set("%s", "Long press to cancel");
      break;
    case core::WifiSetupPhase::kWaitingForPhone:
      ShowCredentials(true);
      if (setup.LastJoinFailed()) {
        status.Set("Couldn't join %.*s. Check the password on your phone.", ssid_len, ssid.data());
      } else {
        status.Set("%s", "Then fill in the page that opens");
      }
      hint.Set("%s", "Long press to cancel");
      break;
    case core::WifiSetupPhase::kJoining:
      ShowCredentials(true);
      status.Set("Joining %.*s...", ssid_len, ssid.data());
      hint.Set("%s", "Long press to cancel");
      break;
    case core::WifiSetupPhase::kJoined:
      ShowCredentials(false);
      status.Set("Connected to %.*s. Saved for updates.", ssid_len, ssid.data());
      hint.Set("%s", "Click to close");
      break;
    case core::WifiSetupPhase::kFailed:
      ShowCredentials(false);
      status.Set("%s", setup.Failure() == core::WifiSetupFailure::kTimedOut
                           ? "Setup timed out. Try again from Settings."
                           : "Couldn't start the setup network.");
      hint.Set("%s", "Click to close");
      break;
  }
}

}  // namespace sarathi::ui::wifi_setup_screen
