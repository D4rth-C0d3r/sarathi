#include "update_screen.h"

#include "layout.h"
#include "ring_gauge.h"
#include "static_label.h"
#include "theme_binding.h"

namespace sarathi::ui::update_screen {
namespace {

constexpr int32_t kTitleY = -150;
constexpr int32_t kHeadlineY = -60;
constexpr int32_t kDetailY = 30;
constexpr int32_t kHintY = 140;
constexpr int32_t kDetailWidth = 360;
constexpr int32_t kRingDiameter = 470;
constexpr uint64_t kBytesPerMb = 1000ULL * 1000ULL;

// NOLINTBEGIN(cppcoreguidelines-avoid-non-const-global-variables)
RingGauge progress;
StaticLabel<24> title;
StaticLabel<32> headline;
StaticLabel<96> detail;
StaticLabel<32> hint;
uint32_t shown_revision = UINT32_MAX;
core::OtaPhase shown_phase = core::OtaPhase::kIdle;
// NOLINTEND(cppcoreguidelines-avoid-non-const-global-variables)

const char* FailureText(const core::OtaUpdate& ota) {
  switch (ota.Failure()) {
    case core::OtaFailure::kNoWifiSetup:
      return "Set up Wi-Fi first, in Settings";
    case core::OtaFailure::kWifiConnect:
      return "Couldn't join the saved Wi-Fi network";
    case core::OtaFailure::kNetwork:
      return "Couldn't reach the update server";
    case core::OtaFailure::kManifest:
      return "The update didn't pass its security checks";
    case core::OtaFailure::kDownload:
      return "The download stopped. Try again.";
    case core::OtaFailure::kImage:
      return "The download was damaged. Try again.";
    case core::OtaFailure::kFlash:
      return "Couldn't write the update";
    case core::OtaFailure::kWeakWifi:
      return "Wi-Fi signal too weak. Move closer to the router.";
    case core::OtaFailure::kNone:
      break;
  }
  return "";
}

}  // namespace

lv_obj_t* Create() {
  lv_obj_t* screen = theme::MakeScreen();
  progress.Create(screen, kRingDiameter);
  progress.SetVisible(false);
  title.Create(screen, theme::Text::kStoppedOnly, kTitleY);
  title.Set("%s", "Software update");
  headline.Create(screen, theme::Text::kEssential, kHeadlineY, layout::kContentWidth);
  detail.Create(screen, theme::Text::kStoppedOnly, kDetailY, kDetailWidth);
  lv_label_set_long_mode(detail.Obj(), LV_LABEL_LONG_WRAP);
  hint.Create(screen, theme::Text::kStoppedOnly, kHintY, layout::kContentWidth);
  return screen;
}

void Update(const core::OtaUpdate& ota, const core::WifiSetup& wifi) {
  // The phase too: a restarted state machine (the simulator's pretend reboot) counts from 0.
  if (ota.Revision() == shown_revision && ota.Phase() == shown_phase) {
    return;
  }
  shown_revision = ota.Revision();
  shown_phase = ota.Phase();
  progress.SetVisible(ota.Phase() == core::OtaPhase::kDownloading);
  switch (ota.Phase()) {
    case core::OtaPhase::kIdle:
    case core::OtaPhase::kConnecting:
      headline.Set("%s", "Checking...");
      detail.Set("Joining %.*s", static_cast<int>(wifi.SavedSsid().size()),
                 wifi.SavedSsid().data());
      hint.Set("%s", "Long press to cancel");
      break;
    case core::OtaPhase::kChecking:
      headline.Set("%s", "Checking...");
      detail.Set("%s", "Looking for a new version");
      hint.Set("%s", "Long press to cancel");
      break;
    case core::OtaPhase::kUpToDate:
      headline.Set("%s", "Up to date");
      detail.Set("%s", "This is the newest version");
      hint.Set("%s", "Click to close");
      break;
    case core::OtaPhase::kAvailable: {
      headline.Set("Version %s", ota.AvailableVersion().CStr());
      const auto tenths =
          static_cast<unsigned>((ota.AvailableSize() * 10 + kBytesPerMb / 2) / kBytesPerMb);
      detail.Set("%u.%u MB. Keep the bike stopped while it installs.", tenths / 10, tenths % 10);
      hint.Set("%s", "Click to install");
      break;
    }
    case core::OtaPhase::kDownloading:
      headline.Set("Updating %u%%", static_cast<unsigned>(ota.ProgressPercent()));
      progress.Set(static_cast<float>(ota.ProgressPercent()) / 100.0F);
      detail.Set("%s", "Keep the bike stopped");
      hint.Set("%s", "Long press to cancel");
      break;
    case core::OtaPhase::kInstalling:
      headline.Set("%s", "Installing...");
      detail.Set("%s", "The screen goes dark for a few seconds");
      hint.Set("%s", "");
      break;
    case core::OtaPhase::kRebooting:
      headline.Set("%s", "Restarting...");
      detail.Set("%s", "The new version starts in a moment");
      hint.Set("%s", "");
      break;
    case core::OtaPhase::kFailed:
      headline.Set("%s", "Update failed");
      detail.Set("%s", FailureText(ota));
      hint.Set("%s", "Click to close");
      break;
  }
}

}  // namespace sarathi::ui::update_screen
