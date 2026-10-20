#include "music_screen.h"

#include <array>

#include "format.h"
#include "placeholder_style.h"
#include "static_label.h"

namespace sarathi::ui::music_screen {
namespace {

constexpr int32_t kButtonSize = 96;
constexpr int32_t kButtonGap = 112;  // centre to centre
constexpr int32_t kButtonsY = 115;

// NOLINTBEGIN(cppcoreguidelines-avoid-non-const-global-variables)
core::AppModel* app = nullptr;
StaticLabel<56> title;
StaticLabel<56> artist;
StaticLabel<24> position;
lv_obj_t* play_label = nullptr;
bool shown_playing = false;
// NOLINTEND(cppcoreguidelines-avoid-non-const-global-variables)

void OnPrev(lv_event_t* /*event*/) { app->MediaPrev(); }
void OnPlayPause(lv_event_t* /*event*/) { app->MediaPlayPause(); }
void OnNext(lv_event_t* /*event*/) { app->MediaNext(); }

lv_obj_t* AddControl(lv_obj_t* screen, lv_group_t* group, const char* symbol, int32_t x,
                     lv_event_cb_t on_click) {
  lv_obj_t* button = style::MakeButton(screen, symbol, style::Essential(), style::kNeutralFill,
                                       kButtonSize, kButtonSize);
  lv_obj_align(button, LV_ALIGN_CENTER, x, kButtonsY);
  lv_obj_add_event_cb(button, on_click, LV_EVENT_CLICKED, nullptr);
  lv_group_add_obj(group, button);
  return button;
}

}  // namespace

lv_obj_t* Create(lv_group_t* group, core::AppModel& model) {
  app = &model;
  lv_obj_t* screen = style::MakeScreen();
  title.Create(screen, style::Essential(), style::kText, -95, style::kContentWidth);
  artist.Create(screen, style::Supporting(), style::kTextSupporting, -35, style::kContentWidth);
  position.Create(screen, style::Supporting(), style::kTextSupporting, 20, style::kContentWidth);

  AddControl(screen, group, LV_SYMBOL_PREV, -kButtonGap, OnPrev);
  lv_obj_t* play = AddControl(screen, group, LV_SYMBOL_PAUSE, 0, OnPlayPause);
  play_label = lv_obj_get_child(play, 0);
  shown_playing = true;
  AddControl(screen, group, LV_SYMBOL_NEXT, kButtonGap, OnNext);
  lv_group_focus_obj(play);
  return screen;
}

void Update(const core::AppState& state) {
  const core::MediaState& media = state.media;
  if (!media.available) {
    title.Set("%s", "No media");
    artist.Set("%s", "");
    position.Set("%s", "");
    return;
  }
  title.Set("%s", media.title.CStr());
  artist.Set("%s", media.artist.CStr());
  std::array<char, 12> pos{};
  std::array<char, 12> dur{};
  core::FormatClock(media.position_s, pos.data(), pos.size());
  core::FormatClock(media.duration_s, dur.data(), dur.size());
  position.Set("%s / %s", pos.data(), dur.data());

  if (media.playing != shown_playing) {
    // The button shows the action it will take.
    lv_label_set_text_static(play_label, media.playing ? LV_SYMBOL_PAUSE : LV_SYMBOL_PLAY);
    shown_playing = media.playing;
  }
}

}  // namespace sarathi::ui::music_screen
