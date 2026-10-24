#include "screen_manager.h"

#include <array>
#include <cstddef>

#include "call_screen.h"
#include "input_test_screen.h"
#include "music_screen.h"
#include "nav_screen.h"
#include "settings_screen.h"
#include "update_screen.h"
#include "wifi_form.h"
#include "wifi_setup_screen.h"

namespace sarathi::ui::screen_manager {
namespace {

constexpr size_t kScreenCount = 7;

// NOLINTBEGIN(cppcoreguidelines-avoid-non-const-global-variables)
lv_indev_t* knob = nullptr;
std::array<lv_obj_t*, kScreenCount> screens{};
std::array<lv_group_t*, kScreenCount> groups{};
ScreenId current = ScreenId::kNav;
ScreenId before_call = ScreenId::kNav;
ScreenId before_input_test = ScreenId::kSettings;
core::OtaUpdate* ota = nullptr;
core::WifiSetup* wifi = nullptr;
uint32_t (*random_source)() = nullptr;
// NOLINTEND(cppcoreguidelines-avoid-non-const-global-variables)

size_t Index(ScreenId id) { return static_cast<size_t>(id); }

void Show(ScreenId id) {
  current = id;
  lv_indev_set_group(knob, groups[Index(id)]);
  lv_screen_load(screens[Index(id)]);
}

void StartWifiSetup() {
  std::array<char, core::kSetupPasswordLength + 1> password{};
  core::MakeSetupPassword(random_source, password.data());
  if (wifi->Start(lv_tick_get(), {password.data(), core::kSetupPasswordLength})) {
    Show(ScreenId::kWifiSetup);
  }
}

void CheckForUpdate() {
  if (wifi->Active()) {
    return;  // the radio is busy with setup
  }
  static_cast<void>(ota->Check(wifi->HasSaved()));  // false: one is already running; show it
  Show(ScreenId::kUpdate);
}

void ForgetWifi() { static_cast<void>(wifi->Forget()); }

}  // namespace

void Init(lv_indev_t* encoder, core::AppModel& model, core::OtaUpdate& ota_update,
          core::WifiSetup& wifi_setup, uint32_t (*random)()) {
  knob = encoder;
  ota = &ota_update;
  wifi = &wifi_setup;
  random_source = random;
  for (auto& group : groups) {
    group = lv_group_create();
  }
  screens[Index(ScreenId::kNav)] = nav_screen::Create();
  screens[Index(ScreenId::kMusic)] = music_screen::Create(groups[Index(ScreenId::kMusic)], model);
  const settings_screen::Actions actions{OpenInputTest, StartWifiSetup, CheckForUpdate, ForgetWifi};
  screens[Index(ScreenId::kSettings)] =
      settings_screen::Create(groups[Index(ScreenId::kSettings)], model, actions);
  screens[Index(ScreenId::kCall)] = call_screen::Create(groups[Index(ScreenId::kCall)], model);
  screens[Index(ScreenId::kInputTest)] = input_test_screen::Create();
  screens[Index(ScreenId::kWifiSetup)] = wifi_setup_screen::Create();
  screens[Index(ScreenId::kUpdate)] = update_screen::Create();
  Show(ScreenId::kNav);
}

void Update(const core::AppModel& model, const core::InputTestState& input_test, uint32_t now_ms) {
  const core::AppState& state = model.State();

  // An incoming or active call takes over the screen; afterwards, go back.
  const bool in_call = state.call.phase != core::CallPhase::kIdle;
  if (in_call && current != ScreenId::kCall) {
    before_call = current;
    call_screen::Update(state, now_ms);  // set up buttons before showing
    Show(ScreenId::kCall);
  } else if (!in_call && current == ScreenId::kCall) {
    Show(before_call);
  }

  // A flow that ended without a knob press (cancelled, nothing to do) closes its screen.
  if ((current == ScreenId::kWifiSetup && wifi->Phase() == core::WifiSetupPhase::kIdle) ||
      (current == ScreenId::kUpdate && ota->Phase() == core::OtaPhase::kIdle)) {
    Show(ScreenId::kSettings);
  }

  switch (current) {
    case ScreenId::kNav:
      nav_screen::Update(state, now_ms);
      break;
    case ScreenId::kMusic:
      music_screen::Update(state);
      break;
    case ScreenId::kSettings:
      settings_screen::Update(state, model.SettingsLocked(), *wifi);
      break;
    case ScreenId::kCall:
      call_screen::Update(state, now_ms);
      break;
    case ScreenId::kInputTest:
      input_test_screen::Update(input_test);
      break;
    case ScreenId::kWifiSetup:
      wifi_setup_screen::Update(*wifi);
      break;
    case ScreenId::kUpdate:
      update_screen::Update(*ota, *wifi);
      break;
  }
}

void NextScreen(const core::AppModel& model) {
  switch (current) {
    case ScreenId::kNav:
      Show(ScreenId::kMusic);
      break;
    case ScreenId::kMusic:
      // Settings can't be used while moving, so don't offer it (CLAUDE.md).
      Show(model.SettingsLocked() ? ScreenId::kNav : ScreenId::kSettings);
      break;
    case ScreenId::kSettings:
      Show(ScreenId::kNav);
      break;
    case ScreenId::kCall:
    case ScreenId::kInputTest:
    case ScreenId::kWifiSetup:
    case ScreenId::kUpdate:
      break;
  }
}

void OpenInputTest() {
  if (current != ScreenId::kInputTest) {
    before_input_test = current;
    Show(ScreenId::kInputTest);
  }
}

void CloseInputTest() {
  if (current == ScreenId::kInputTest) {
    Show(before_input_test);
  }
}

bool ModalOpen() { return current == ScreenId::kWifiSetup || current == ScreenId::kUpdate; }

void ModalClick() {
  if (current == ScreenId::kWifiSetup) {
    if (wifi->Close()) {  // only once setup has finished
      Show(ScreenId::kSettings);
    }
  } else if (current == ScreenId::kUpdate) {
    switch (ota->Phase()) {
      case core::OtaPhase::kAvailable:
        static_cast<void>(ota->Confirm());
        break;
      case core::OtaPhase::kIdle:
      case core::OtaPhase::kUpToDate:
      case core::OtaPhase::kFailed:
        Show(ScreenId::kSettings);
        break;
      default:
        break;  // busy: a click does nothing
    }
  }
}

void ModalLongPress() {
  if (current == ScreenId::kWifiSetup) {
    if (!wifi->Cancel()) {
      static_cast<void>(wifi->Close());
    }
    Show(ScreenId::kSettings);
  } else if (current == ScreenId::kUpdate) {
    const core::OtaPhase phase = ota->Phase();
    if (phase == core::OtaPhase::kInstalling || phase == core::OtaPhase::kRebooting) {
      return;  // too late to cancel
    }
    static_cast<void>(ota->Cancel());
    Show(ScreenId::kSettings);
  }
}

ScreenId Current() { return current; }

}  // namespace sarathi::ui::screen_manager
