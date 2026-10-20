#include "screen_manager.h"

#include <array>
#include <cstddef>

#include "call_screen.h"
#include "input_test_screen.h"
#include "music_screen.h"
#include "nav_screen.h"
#include "settings_screen.h"

namespace sarathi::ui::screen_manager {
namespace {

constexpr size_t kScreenCount = 5;

// NOLINTBEGIN(cppcoreguidelines-avoid-non-const-global-variables)
lv_indev_t* knob = nullptr;
std::array<lv_obj_t*, kScreenCount> screens{};
std::array<lv_group_t*, kScreenCount> groups{};
ScreenId current = ScreenId::kNav;
ScreenId before_call = ScreenId::kNav;
ScreenId before_input_test = ScreenId::kSettings;
// NOLINTEND(cppcoreguidelines-avoid-non-const-global-variables)

size_t Index(ScreenId id) { return static_cast<size_t>(id); }

void Show(ScreenId id) {
  current = id;
  lv_indev_set_group(knob, groups[Index(id)]);
  lv_screen_load(screens[Index(id)]);
}

}  // namespace

void Init(lv_indev_t* encoder, core::AppModel& model) {
  knob = encoder;
  for (auto& group : groups) {
    group = lv_group_create();
  }
  screens[Index(ScreenId::kNav)] = nav_screen::Create();
  screens[Index(ScreenId::kMusic)] = music_screen::Create(groups[Index(ScreenId::kMusic)], model);
  screens[Index(ScreenId::kSettings)] =
      settings_screen::Create(groups[Index(ScreenId::kSettings)], model, OpenInputTest);
  screens[Index(ScreenId::kCall)] = call_screen::Create(groups[Index(ScreenId::kCall)], model);
  screens[Index(ScreenId::kInputTest)] = input_test_screen::Create();
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

  switch (current) {
    case ScreenId::kNav:
      nav_screen::Update(state, now_ms);
      break;
    case ScreenId::kMusic:
      music_screen::Update(state);
      break;
    case ScreenId::kSettings:
      settings_screen::Update(state, model.SettingsLocked());
      break;
    case ScreenId::kCall:
      call_screen::Update(state, now_ms);
      break;
    case ScreenId::kInputTest:
      input_test_screen::Update(input_test);
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

ScreenId Current() { return current; }

}  // namespace sarathi::ui::screen_manager
