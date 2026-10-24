#include "settings_screen.h"

#include <array>
#include <cstdio>

#include "layout.h"
#include "list_menu.h"
#include "static_label.h"
#include "theme_binding.h"
#include "theme_registry.h"
#include "version.h"

namespace sarathi::ui::settings_screen {
namespace {

constexpr int32_t kTitleY = -175;
constexpr int32_t kMenuY = 10;
constexpr int32_t kMenuWidth = 340;
constexpr int32_t kMenuMaxHeight = 300;  // longer lists scroll, focused row centred
constexpr int kBrightnessStep = 5;

enum class Page : uint8_t { kMain, kThemes };

// NOLINTBEGIN(cppcoreguidelines-avoid-non-const-global-variables)
core::AppModel* app = nullptr;
lv_group_t* focus_group = nullptr;
Actions actions_{};
StaticLabel<24> title;
StaticLabel<40> lock_message;
ListMenu main_menu;
ListMenu theme_menu;
size_t theme_row = 0;  // "Theme: ..." in the main menu
size_t day_night_row = 0;
size_t units_row = 0;
size_t timeout_row = 0;
size_t knob_row = 0;
size_t wifi_row = 0;
size_t forget_row = 0;
size_t phones_row = 0;  // Forget phones: only with BLE pairing (Chronos build)
std::array<char, 40> version_text{};
std::array<size_t, ListMenu::kMaxRows> theme_indices{};
Page page = Page::kMain;
bool shown_locked = true;
core::ThemeId shown_theme = core::ThemeId::kRacing;  // forces the first refresh
// NOLINTEND(cppcoreguidelines-avoid-non-const-global-variables)

void ShowPage(Page next) {
  main_menu.Hide();
  theme_menu.Hide();
  page = next;
  if (next == Page::kMain) {
    title.Set("%s", "Settings");
    main_menu.Show(focus_group);
  } else {
    title.Set("%s", "Theme");
    theme_menu.Show(focus_group);
  }
}

void OpenThemes(void* /*context*/) { ShowPage(Page::kThemes); }
void Back(void* /*context*/) { ShowPage(Page::kMain); }
void OpenInputTest(void* /*context*/) { actions_.open_input_test(); }
void StartWifiSetup(void* /*context*/) { actions_.start_wifi_setup(); }
void CheckForUpdate(void* /*context*/) { actions_.check_for_update(); }
void ForgetWifi(void* /*context*/) { actions_.forget_wifi(); }
void ForgetPhones(void* /*context*/) { app->ForgetPhones(); }
void Nothing(void* /*context*/) {}

void SelectTheme(void* context) {
  const size_t index = *static_cast<const size_t*>(context);
  app->SetTheme(core::ThemeAt(index).id);  // the shell applies it on the next update
  ShowPage(Page::kMain);
}

int GetBrightness(void* /*context*/) { return app->State().settings.brightness_percent; }
void SetBrightness(void* /*context*/, int value) { app->SetBrightness(value); }

// Toggle and cycle rows: a click changes the setting; Update() shows the new value.
void ToggleDayNight(void* /*context*/) {
  app->SetAutoDayNight(!app->State().settings.auto_day_night);
}
void ToggleUnits(void* /*context*/) {
  app->SetUnits(app->State().settings.units == core::Units::kMetric ? core::Units::kImperial
                                                                    : core::Units::kMetric);
}
void CycleTimeout(void* /*context*/) {
  const auto next =
      (static_cast<size_t>(app->State().settings.screen_timeout) + 1) % core::kScreenTimeoutCount;
  app->SetScreenTimeout(static_cast<core::ScreenTimeout>(next));
}
void ToggleKnob(void* /*context*/) { app->SetKnobReversed(!app->State().settings.knob_reversed); }

const char* TimeoutName(core::ScreenTimeout t) {
  switch (t) {
    case core::ScreenTimeout::k30s:
      return "30 s";
    case core::ScreenTimeout::k1min:
      return "1 min";
    case core::ScreenTimeout::k5min:
      return "5 min";
    case core::ScreenTimeout::kNever:
      break;
  }
  return "Never";
}

void RefreshSettingTexts(const core::Settings& s) {
  std::array<char, 40> text{};
  std::snprintf(text.data(), text.size(), "Day/night: %s", s.auto_day_night ? "Auto" : "Off");
  main_menu.SetText(day_night_row, text.data());
  std::snprintf(text.data(), text.size(), "Units: %s",
                s.units == core::Units::kMetric ? "Metric" : "Imperial");
  main_menu.SetText(units_row, text.data());
  std::snprintf(text.data(), text.size(), "Screen off: %s", TimeoutName(s.screen_timeout));
  main_menu.SetText(timeout_row, text.data());
  std::snprintf(text.data(), text.size(), "Knob: %s", s.knob_reversed ? "Reversed" : "Normal");
  main_menu.SetText(knob_row, text.data());
}

void RefreshWifiTexts(const core::WifiSetup& wifi) {
  std::array<char, 48> text{};
  if (wifi.HasSaved()) {
    std::snprintf(text.data(), text.size(), "Wi-Fi: %.*s",
                  static_cast<int>(wifi.SavedSsid().size()), wifi.SavedSsid().data());
  } else {
    std::snprintf(text.data(), text.size(), "%s", "Wi-Fi: Set up");
  }
  main_menu.SetText(wifi_row, text.data());
  main_menu.SetText(forget_row, wifi.HasSaved() ? "Forget Wi-Fi" : "No Wi-Fi saved");
}

void RefreshThemeTexts(core::ThemeId current) {
  std::array<char, 40> text{};
  std::snprintf(text.data(), text.size(), "Theme: %s", core::ThemeFor(current).short_name);
  main_menu.SetText(theme_row, text.data());
  for (size_t i = 0; i < core::ThemeCount() && i + 1 < ListMenu::kMaxRows; ++i) {
    const core::ThemeDescriptor& t = core::ThemeAt(i);
    std::snprintf(text.data(), text.size(), "%s %s", t.id == current ? LV_SYMBOL_OK : "  ", t.name);
    theme_menu.SetText(i, text.data());
  }
  shown_theme = current;
}

void SetLocked(bool locked) {
  if (locked) {
    main_menu.Hide();
    theme_menu.Hide();
    lv_obj_add_flag(title.Obj(), LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(lock_message.Obj(), LV_OBJ_FLAG_HIDDEN);
  } else {
    lv_obj_remove_flag(title.Obj(), LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(lock_message.Obj(), LV_OBJ_FLAG_HIDDEN);
    ShowPage(Page::kMain);
  }
  shown_locked = locked;
}

}  // namespace

lv_obj_t* Create(lv_group_t* group, core::AppModel& model, const Actions& actions) {
  app = &model;
  focus_group = group;
  actions_ = actions;
  lv_obj_t* screen = theme::MakeScreen();

  title.Create(screen, theme::Text::kStoppedOnly, kTitleY);

  main_menu.Create(screen, kMenuY, kMenuWidth, kMenuMaxHeight);
  theme_row = main_menu.AddAction("Theme", OpenThemes, nullptr);
  main_menu.AddValue("Brightness", "%", 5, 100, kBrightnessStep, GetBrightness, SetBrightness,
                     nullptr);
  day_night_row = main_menu.AddAction("Day/night", ToggleDayNight, nullptr);
  units_row = main_menu.AddAction("Units", ToggleUnits, nullptr);
  timeout_row = main_menu.AddAction("Screen off", CycleTimeout, nullptr);
  knob_row = main_menu.AddAction("Knob", ToggleKnob, nullptr);
  wifi_row = main_menu.AddAction("Wi-Fi", StartWifiSetup, nullptr);
  main_menu.AddAction("Check for update", CheckForUpdate, nullptr);
  forget_row = main_menu.AddAction("Forget Wi-Fi", ForgetWifi, nullptr);
  phones_row = main_menu.AddAction("Forget phones", ForgetPhones, nullptr);
  main_menu.SetHidden(phones_row, true);
  std::snprintf(version_text.data(), version_text.size(), "Version %.*s",
                static_cast<int>(core::kFirmwareVersion.size()), core::kFirmwareVersion.data());
  main_menu.AddAction(version_text.data(), Nothing, nullptr);
  main_menu.AddAction("Input test", OpenInputTest, nullptr);

  theme_menu.Create(screen, kMenuY, kMenuWidth, kMenuMaxHeight);
  for (size_t i = 0; i < core::ThemeCount() && i + 1 < ListMenu::kMaxRows; ++i) {
    theme_indices.at(i) = i;
    theme_menu.AddAction(core::ThemeAt(i).name, SelectTheme, &theme_indices.at(i));
  }
  theme_menu.AddAction(LV_SYMBOL_LEFT " Back", Back, nullptr);

  lock_message.Create(screen, theme::Text::kWarning, 0, layout::kContentWidth);
  lv_label_set_long_mode(lock_message.Obj(), LV_LABEL_LONG_WRAP);
  lock_message.Set("%s", "Stop to change settings");

  main_menu.Hide();
  theme_menu.Hide();
  SetLocked(false);
  return screen;
}

void Update(const core::AppState& state, bool locked, const core::WifiSetup& wifi) {
  if (locked != shown_locked) {
    SetLocked(locked);
  }
  if (state.settings.theme != shown_theme) {
    RefreshThemeTexts(state.settings.theme);
  }
  RefreshSettingTexts(state.settings);  // SetText skips unchanged text
  RefreshWifiTexts(wifi);
  main_menu.SetHidden(phones_row, !state.caps.phone_pairing);
  std::array<char, 40> text{};
  if (state.phone.paired_phones > 0) {
    std::snprintf(text.data(), text.size(), "Forget phones (%u)",
                  static_cast<unsigned>(state.phone.paired_phones));
  } else {
    std::snprintf(text.data(), text.size(), "%s", "No phones paired");
  }
  main_menu.SetText(phones_row, text.data());
  main_menu.Refresh();
}

}  // namespace sarathi::ui::settings_screen
