// sārathi desktop simulator: runs the same sarathi_ui shell and sarathi_core logic (with the
// demo ride) as the device, on LVGL's SDL2 driver at 480x480 with the round glass masked off.
//
// Input, translated into what the hardware produces:
//   Left/Down arrow, wheel down  -> knob step -1
//   Right/Up arrow, wheel up     -> knob step +1
//   Enter (held)                 -> knob button (click, double-click, hold = long press)
//   Mouse button inside circle   -> touch at that point
//   S                            -> pause/resume the demo ride (the bike stops; settings unlock)
// Usage: program [--speed N]   replay the ride N times faster. Close the window to quit.
//        program --tour DIR     scripted walk through the UI, screenshots in DIR
//        program --gallery DIR  every screen in every theme, screenshots in DIR
//        add --chronos          a pretend Chronos phone instead of the demo ride (ADR-0005)
//
// Simulator wiring only: no backlight, panel timing or ESP32-S3 performance limits.

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>
#include <string>
#include <vector>

#include <SDL2/SDL.h>
#include <lvgl.h>

#include "app_model.h"
#include "app_shell.h"
#include "board_pins.h"
#include "demo_ride.h"
#include "fake_chronos.h"
#include "input_test_model.h"
#include "lvgl_port.h"
#include "ota_update.h"
#include "screen_manager.h"
#include "settings_codec.h"
#include "settings_saver.h"
#include "theme_registry.h"
#include "wifi_setup.h"

namespace {

namespace core = sarathi::core;
namespace ui = sarathi::ui;
namespace pins = sarathi::board::pins;

constexpr int32_t kSize = pins::kScreenWidth;
constexpr int32_t kCenter = kSize / 2;
constexpr int32_t kRadius = kSize / 2;
constexpr uint32_t kLoopDelayMs = 5;
constexpr uint32_t kPerfPeriodMs = 5000;

// NOLINTBEGIN(cppcoreguidelines-avoid-non-const-global-variables)
core::AppModel app;
core::DemoRide demo_ride;
sarathi::sim::FakeChronos fake_chronos;
core::DataSource* source = &demo_ride;  // or &fake_chronos with --chronos
bool chronos_mode = false;
core::InputTestModel input_test(pins::kKnobDebounceMs, pins::kKnobDoubleClickMs,
                                pins::kBacklightDefaultPercent, core::kBacklightStepPercent);
int pending_steps = 0;  // written by the SDL event watch, drained by the loop
bool pause_toggle_requested = false;
std::vector<uint8_t> mask_pixels;     // ARGB8888, allocated once at startup
lv_obj_t* backlight_shade = nullptr;  // darkens the window like the real backlight
int shown_backlight = -1;
core::SettingsSaver settings_saver;
std::string settings_path = "sim_settings.bin";
core::OtaUpdate ota_update;
core::WifiSetup wifi_setup;
// NOLINTEND(cppcoreguidelines-avoid-non-const-global-variables)

bool InsideCircle(int32_t x, int32_t y) {
  const int32_t dx = x - kCenter;
  const int32_t dy = y - kCenter;
  return (dx * dx) + (dy * dy) <= kRadius * kRadius;
}

// LVGL's SDL driver drains the SDL event queue itself, so key and wheel events are observed
// with an event watch instead of being taken from the queue. Runs on the main thread during
// lv_timer_handler()'s event pump.
int WatchInput(void* /*unused*/, SDL_Event* event) {
  if (event->type == SDL_KEYDOWN && event->key.repeat == 0) {
    switch (event->key.keysym.sym) {
      case SDLK_LEFT:
      case SDLK_DOWN:
        --pending_steps;
        break;
      case SDLK_RIGHT:
      case SDLK_UP:
        ++pending_steps;
        break;
      case SDLK_s:
        pause_toggle_requested = true;
        break;
      default:
        break;
    }
  } else if (event->type == SDL_MOUSEWHEEL) {
    const int notches =
        event->wheel.direction == SDL_MOUSEWHEEL_FLIPPED ? -event->wheel.y : event->wheel.y;
    pending_steps += notches;
  }
  return 0;
}

bool EnterHeld() {
  const Uint8* keys = SDL_GetKeyboardState(nullptr);
  return keys[SDL_SCANCODE_RETURN] != 0 || keys[SDL_SCANCODE_KP_ENTER] != 0;
}

// Mouse as touch. Presses outside the circle are ignored, like the round glass.
void ReadTouch(lv_indev_t* /*indev*/, lv_indev_data_t* data) {
  int x = 0;
  int y = 0;
  const Uint32 buttons = SDL_GetMouseState(&x, &y);
  const bool pressed = SDL_GetMouseFocus() != nullptr &&
                       (buttons & SDL_BUTTON(SDL_BUTTON_LEFT)) != 0 && InsideCircle(x, y);
  // false while the touch that woke the screen is held.
  const bool pass =
      ui::app_shell::OnTouch(pressed, static_cast<int16_t>(x), static_cast<int16_t>(y));
  if (pass) {
    data->point.x = x;
    data->point.y = y;
    data->state = LV_INDEV_STATE_PRESSED;
  } else {
    data->state = LV_INDEV_STATE_RELEASED;
  }
}

// Black outside the visible circle, transparent inside, with a one-pixel soft edge. Drawn on
// LVGL's system layer, above every screen.
void CreateRoundMask() {
  constexpr int32_t kBytesPerPixel = 4;  // ARGB8888, stored as B, G, R, A
  mask_pixels.assign(static_cast<size_t>(kSize) * kSize * kBytesPerPixel, 0);
  for (int32_t y = 0; y < kSize; ++y) {
    for (int32_t x = 0; x < kSize; ++x) {
      const float dx = static_cast<float>(x - kCenter) + 0.5F;
      const float dy = static_cast<float>(y - kCenter) + 0.5F;
      const float outside = SDL_sqrtf((dx * dx) + (dy * dy)) - static_cast<float>(kRadius);
      const float coverage = outside <= 0.0F ? 0.0F : (outside >= 1.0F ? 1.0F : outside);
      mask_pixels[(static_cast<size_t>(y) * kSize + x) * kBytesPerPixel + 3] =
          static_cast<uint8_t>(coverage * 255.0F);
    }
  }
  lv_obj_t* canvas = lv_canvas_create(lv_layer_sys());
  lv_canvas_set_buffer(canvas, mask_pixels.data(), kSize, kSize, LV_COLOR_FORMAT_ARGB8888);
  lv_obj_set_pos(canvas, 0, 0);
  lv_obj_remove_flag(canvas, LV_OBJ_FLAG_CLICKABLE);
}

// ---- Settings persistence: a file instead of NVS ----

void LoadSettingsFile() {
  core::SettingsBlob blob{};
  size_t read = 0;
  if (std::FILE* f = std::fopen(settings_path.c_str(), "rb")) {
    read = std::fread(blob.data(), 1, blob.size() + 1, f);  // +1: detect an over-long file
    std::fclose(f);
  }
  const bool found = read == blob.size();
  app.LoadSettings(found ? core::DecodeSettings(blob.data(), read) : core::Settings{});
  settings_saver.MarkSaved(app.SettingsRevision());
  std::printf("sim: settings %s (%s)\n", found ? "loaded" : "defaults", settings_path.c_str());
}

void SaveSettingsFile() {
  const core::SettingsBlob blob = core::EncodeSettings(app.State().settings);
  std::FILE* f = std::fopen(settings_path.c_str(), "wb");
  const bool ok = f != nullptr && std::fwrite(blob.data(), 1, blob.size(), f) == blob.size();
  if (f != nullptr) {
    std::fclose(f);
  }
  if (ok) {
    std::printf("sim: settings saved\n");
  } else {
    std::fprintf(stderr, "sim: could not save %s\n", settings_path.c_str());
    settings_saver.SaveFailed(SDL_GetTicks());
  }
}

// The simulator has no backlight; a black layer of matching opacity stands in for it (0 % is
// fully black, like the screen timeout).
void CreateBacklightShade() {
  backlight_shade = lv_obj_create(lv_layer_sys());
  lv_obj_remove_style_all(backlight_shade);
  lv_obj_set_size(backlight_shade, kSize, kSize);
  lv_obj_set_style_bg_color(backlight_shade, lv_color_black(), 0);
  lv_obj_set_style_bg_opa(backlight_shade, LV_OPA_TRANSP, 0);
  lv_obj_remove_flag(backlight_shade, LV_OBJ_FLAG_CLICKABLE);
}

void ShowBacklight(int percent) {
  if (percent != shown_backlight) {
    shown_backlight = percent;
    lv_obj_set_style_bg_opa(backlight_shade, static_cast<lv_opa_t>(255 - (percent * 255 / 100)), 0);
  }
}

// ---- A pretend Wi-Fi, update server and phone, so the setup and update screens can be tried
// (and screenshotted) on the desktop. Timings are short but not instant.

constexpr uint32_t kFakeConnectMs = 500;
constexpr uint32_t kFakeManifestMs = 800;
constexpr uint32_t kFakeChunkMs = 100;
constexpr uint64_t kFakeImageBytes = 1480000;
constexpr uint64_t kFakeChunkBytes = 74000;
constexpr uint32_t kFakeInstallMs = 600;
constexpr uint32_t kFakePhoneMs = 6000;  // the "phone" fills in the page after this
constexpr uint32_t kFakeJoinMs = 1500;

struct FakePlatform {
  core::OtaJob job;
  bool busy = false;
  uint32_t due_ms = 0;
  uint64_t received = 0;
  core::OtaError manifest_error = core::OtaError::kNone;  // what the next check finds
  uint32_t phone_due_ms = 0;
  bool phone_sent = false;
  uint32_t join_due_ms = 0;
};

FakePlatform fake;  // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)

uint32_t SimRandom() {
  static std::random_device device;
  return static_cast<uint32_t>(device());
}

void RunFakeOta(uint32_t now) {
  const core::OtaJob job = ota_update.TakeJob();
  switch (job.type) {
    case core::OtaJobType::kNone:
      break;
    case core::OtaJobType::kAbort:
      fake.busy = false;
      break;
    case core::OtaJobType::kReboot:
      std::printf("sim: would restart into the new version\n");
      ota_update = core::OtaUpdate{};  // as after a reboot
      fake.busy = false;
      return;
    default:
      fake.job = job;
      fake.busy = true;
      fake.received = 0;
      fake.due_ms = now + (job.type == core::OtaJobType::kConnect         ? kFakeConnectMs
                           : job.type == core::OtaJobType::kFetchManifest ? kFakeManifestMs
                           : job.type == core::OtaJobType::kInstall       ? kFakeInstallMs
                                                                          : kFakeChunkMs);
      break;
  }
  if (!fake.busy || now < fake.due_ms) {
    return;
  }
  const uint32_t attempt = fake.job.attempt;
  switch (fake.job.type) {
    case core::OtaJobType::kConnect:
      fake.busy = false;
      ota_update.OnConnected(attempt, wifi_setup.HasSaved());
      break;
    case core::OtaJobType::kFetchManifest: {
      fake.busy = false;
      core::Manifest manifest;
      manifest.version.Assign("0.3.0");
      manifest.size = kFakeImageBytes;
      ota_update.OnManifest(attempt, true, fake.manifest_error, &manifest);
      break;
    }
    case core::OtaJobType::kDownload:
      fake.received += kFakeChunkBytes;
      ota_update.OnProgress(attempt, fake.received);
      if (fake.received >= kFakeImageBytes) {
        fake.busy = false;
        ota_update.OnDownloaded(attempt, true);
      } else {
        fake.due_ms = now + kFakeChunkMs;
      }
      break;
    case core::OtaJobType::kInstall:
      fake.busy = false;
      ota_update.OnInstalled(attempt, core::OtaError::kNone, true);
      break;
    default:
      fake.busy = false;
      break;
  }
}

void RunFakeWifiSetup(uint32_t now) {
  switch (wifi_setup.TakeJob()) {
    case core::WifiSetupJob::kStartAccessPoint:
      wifi_setup.OnAccessPointStarted(true);
      fake.phone_due_ms = now + kFakePhoneMs;
      fake.phone_sent = false;
      break;
    case core::WifiSetupJob::kForget:
      wifi_setup.SetSaved("");
      break;
    default:
      break;
  }
  if (wifi_setup.Phase() == core::WifiSetupPhase::kWaitingForPhone && !fake.phone_sent &&
      now >= fake.phone_due_ms) {
    fake.phone_sent = true;
    wifi_setup.OnSubmitted("Home Wi-Fi");
    fake.join_due_ms = now + kFakeJoinMs;
  }
  if (wifi_setup.Phase() == core::WifiSetupPhase::kJoining && now >= fake.join_due_ms) {
    wifi_setup.OnJoinResult(true);
  }
  wifi_setup.Tick(now);
}

uint32_t TimeUs() {
  return static_cast<uint32_t>(SDL_GetPerformanceCounter() * 1000000ULL /
                               SDL_GetPerformanceFrequency());
}

// Desktop frame times say nothing about the ESP32-S3; this keeps the code path exercised.
void LogPerf() {
  const core::FrameStats::Summary f = ui::lvgl_port::TakeFrameStats();
  std::printf("sim: perf | frames %u/%us | frame avg %.1f ms max %.1f ms\n",
              static_cast<unsigned>(f.count), static_cast<unsigned>(kPerfPeriodMs / 1000),
              static_cast<double>(f.avg_us) / 1000.0, static_cast<double>(f.max_us) / 1000.0);
}

uint32_t ParseSpeed(int argc, char** argv) {
  for (int i = 1; i + 1 < argc; ++i) {
    if (std::strcmp(argv[i], "--speed") == 0) {                  // NOLINT(*-pointer-arithmetic)
      const long speed = std::strtol(argv[i + 1], nullptr, 10);  // NOLINT(*-pointer-arithmetic)
      return speed > 0 ? static_cast<uint32_t>(speed) : 1U;
    }
  }
  return 1;
}

// ---- --tour DIR: a scripted walk through every screen that saves screenshots ----
// It drives the shell exactly like the knob does (button samples and steps), so it doubles as
// an end-to-end check of the UI. Times are from start; the ride runs at 10x, so the call rings
// from 6 s to 8 s. A click reaches LVGL ~400 ms after the press (release + double-click
// window), so the next action waits at least 500 ms.

enum class TourAction : uint8_t { kClick, kDoubleClick, kStepUp, kStepDown, kPause, kShot, kQuit };

struct TourStep {
  uint32_t at_ms;
  TourAction action;
  const char* name;  // screenshot file name (kShot only)
};

constexpr uint32_t kTourSpeed = 10;
constexpr uint32_t kPressMs = 100;
constexpr uint32_t kLongHoldMs = 1000;  // past the 800 ms long press
constexpr uint32_t kDoubleGapMs = 200;

constexpr std::array<TourStep, 34> kTour = {{
    {1000, TourAction::kShot, "01_nav_classic"},
    {6400, TourAction::kShot, "02_call_ringing"},
    {6500, TourAction::kStepUp, nullptr},  // focus Reject
    {6700, TourAction::kClick, nullptr},   // reject: back to Nav
    {7600, TourAction::kPause, nullptr},   // stop the bike so Settings unlocks
    {7700, TourAction::kDoubleClick, nullptr},
    {8600, TourAction::kShot, "03_music_classic"},
    {8800, TourAction::kDoubleClick, nullptr},
    {9700, TourAction::kShot, "04_settings_classic"},
    {9900, TourAction::kClick, nullptr},  // Theme row: open the theme list
    {10900, TourAction::kShot, "05_theme_list"},
    {11000, TourAction::kStepUp, nullptr},  // focus Fighter jet HUD
    {11200, TourAction::kClick, nullptr},   // select it: switches at runtime
    {12200, TourAction::kShot, "06_settings_hud"},
    {12300, TourAction::kStepUp, nullptr},  // focus Brightness
    {12500, TourAction::kClick, nullptr},   // start adjusting
    {13400, TourAction::kStepDown, nullptr},
    {13450, TourAction::kStepDown, nullptr},
    {13800, TourAction::kShot, "07_brightness_editing"},
    {13900, TourAction::kClick, nullptr},   // confirm
    {14500, TourAction::kStepUp, nullptr},  // focus Day/night
    {14700, TourAction::kClick, nullptr},   // Auto
    {15300, TourAction::kStepUp, nullptr},  // focus Units
    {15500, TourAction::kClick, nullptr},   // Imperial: Nav distances in mi/ft
    {16100, TourAction::kStepUp, nullptr},  // focus Screen off: the list scrolls
    {16600, TourAction::kShot, "08_settings_scrolled"},
    {16800, TourAction::kDoubleClick, nullptr},
    {17700, TourAction::kShot, "09_nav_hud"},
    {17900, TourAction::kDoubleClick, nullptr},
    {18700, TourAction::kShot, "10_music_hud"},
    {18900, TourAction::kPause, nullptr},        // resume riding
    {19000, TourAction::kDoubleClick, nullptr},  // Music -> Nav (Settings skipped while moving)
    {20000, TourAction::kShot, "11_nav_hud_moving"},
    {20300, TourAction::kQuit, nullptr},
}};

struct PressWindow {
  uint32_t down_ms = 0;
  uint32_t up_ms = 0;
};

struct Tour {
  const char* dir = nullptr;
  uint32_t start_ms = 0;
  size_t next = 0;
  std::array<PressWindow, 4> presses{};
  size_t press_count = 0;

  void Press(uint32_t at) { Hold(at, kPressMs); }

  void Hold(uint32_t at, uint32_t ms) {
    presses.at(press_count % presses.size()) = {at, at + ms};
    ++press_count;
  }

  [[nodiscard]] bool ButtonDown(uint32_t now) const {
    for (const PressWindow& p : presses) {
      if (now >= p.down_ms && now < p.up_ms) {
        return true;
      }
    }
    return false;
  }
};

void SaveScreenshot(lv_display_t* display, const char* dir, const char* name) {
  lv_refr_now(display);
  auto* renderer = static_cast<SDL_Renderer*>(lv_sdl_window_get_renderer(display));
  SDL_Surface* shot = SDL_CreateRGBSurfaceWithFormat(0, kSize, kSize, 32, SDL_PIXELFORMAT_ARGB8888);
  if (shot == nullptr || renderer == nullptr) {
    std::fprintf(stderr, "sim: screenshot failed: %s\n", SDL_GetError());
    return;
  }
  SDL_RenderReadPixels(renderer, nullptr, SDL_PIXELFORMAT_ARGB8888, shot->pixels, shot->pitch);
  std::array<char, 512> path{};
  std::snprintf(path.data(), path.size(), "%s/%s.bmp", dir, name);
  if (SDL_SaveBMP(shot, path.data()) == 0) {
    std::printf("sim: saved %s\n", path.data());
  } else {
    std::fprintf(stderr, "sim: could not write %s: %s\n", path.data(), SDL_GetError());
  }
  SDL_FreeSurface(shot);
}

// Runs the tour steps that are due. Returns false when the tour has finished.
bool RunTour(Tour& tour, lv_display_t* display, uint32_t now) {
  while (tour.next < kTour.size() && now - tour.start_ms >= kTour.at(tour.next).at_ms) {
    const TourStep& step = kTour.at(tour.next++);
    switch (step.action) {
      case TourAction::kClick:
        tour.Press(now);
        break;
      case TourAction::kDoubleClick:
        tour.Press(now);
        tour.Press(now + kDoubleGapMs);
        break;
      case TourAction::kStepUp:
        ui::app_shell::OnKnobStep(1);
        break;
      case TourAction::kStepDown:
        ui::app_shell::OnKnobStep(-1);
        break;
      case TourAction::kPause:
        demo_ride.SetPaused(!demo_ride.Paused());
        break;
      case TourAction::kShot:
        SaveScreenshot(display, tour.dir, step.name);
        break;
      case TourAction::kQuit:
        return false;
    }
  }
  return true;
}

const char* ParseDirOption(int argc, char** argv, const char* option) {
  for (int i = 1; i + 1 < argc; ++i) {
    if (std::strcmp(argv[i], option) == 0) {  // NOLINT(*-pointer-arithmetic)
      return argv[i + 1];                     // NOLINT(*-pointer-arithmetic)
    }
  }
  return nullptr;
}

// ---- --gallery DIR: every screen in every registered theme ----
// The ride runs for a moment so Nav has a route, then stops (so Settings is offered). For
// each theme the gallery loads it as a saved setting, visits each screen and saves
// "<theme>_<n>_<screen>.bmp". The call is injected over the demo ride's own call state.

enum class GalleryAction : uint8_t {
  kLoadTheme,
  kNav,
  kCallRinging,
  kCallActive,
  kCallEnd,
  kMusic,
  kSettings,
  kThemeList,
  kLeaveThemeList,
  kInputTest,
  kLeaveInputTest,
  kSaveWifi,  // pretend a network was saved earlier
  kKnob,      // `arg` knob steps
  kPress,     // a knob click
  kLongPress,
  kManifest,    // the next update check finds: 0 a new version, 1 a bad signature
  kDisconnect,  // --chronos: the phone drops the link (shown on Nav)
  kReconnect,
  kPairing,  // --chronos: a phone asks to pair
  kStopNav,  // --chronos: navigation ends (Settings unlocks)
  kStartNav,
  kWait,
};

struct GalleryStep {
  GalleryAction action;
  const char* name;  // screenshot taken once the step has settled; nullptr = none
  uint32_t settle_ms;
  int arg = 0;
};

constexpr uint32_t kGalleryWarmupMs = 2500;  // at 10x: the route window is in
constexpr std::array<GalleryStep, 25> kGallerySteps = {{
    {GalleryAction::kLoadTheme, nullptr, 400},
    {GalleryAction::kNav, "1_nav", 600},
    {GalleryAction::kCallRinging, "2_call_ringing", 600},
    {GalleryAction::kCallActive, "3_call_active", 600},
    {GalleryAction::kCallEnd, nullptr, 400},
    {GalleryAction::kMusic, "4_music", 600},
    {GalleryAction::kSettings, "5_settings", 600},
    {GalleryAction::kThemeList, "6_theme_list", 900},  // a knob click on the focused Theme row
    {GalleryAction::kLeaveThemeList, nullptr, 900},    // re-select the current theme
    {GalleryAction::kInputTest, "7_input_test", 600},
    {GalleryAction::kLeaveInputTest, nullptr, 300},
    // Settings rows from the top: 0 Theme ... 6 Wi-Fi, 7 Check for update.
    {GalleryAction::kSaveWifi, nullptr, 100},
    {GalleryAction::kKnob, nullptr, 300, 6},
    {GalleryAction::kPress, "8_wifi_setup", 1200},
    {GalleryAction::kWait, "8b_wifi_joined", 7000},  // the pretend phone joins after ~7.5 s
    {GalleryAction::kPress, nullptr, 1600},          // close; focus stays on the Wi-Fi row
    {GalleryAction::kKnob, nullptr, 300, 1},
    {GalleryAction::kManifest, nullptr, 100, 0},
    {GalleryAction::kPress, "9_update_available", 2200},
    {GalleryAction::kPress, "10_update_downloading", 1300},
    {GalleryAction::kWait, nullptr, 4000},  // finishes, installs, restarts: back to Settings
    {GalleryAction::kManifest, nullptr, 100, 1},
    {GalleryAction::kPress, "11_update_failed", 2200},
    {GalleryAction::kPress, nullptr, 900},     // close
    {GalleryAction::kKnob, nullptr, 300, -7},  // back to the Theme row for the next theme
}};

// --chronos: the screens that differ, in every theme.
constexpr std::array<GalleryStep, 14> kChronosGallerySteps = {{
    {GalleryAction::kLoadTheme, nullptr, 400},
    {GalleryAction::kNav, "c1_nav", 600},
    {GalleryAction::kCallRinging, "c2_call", 600},
    {GalleryAction::kCallEnd, nullptr, 400},
    {GalleryAction::kMusic, "c3_music", 600},
    {GalleryAction::kDisconnect, "c4_disconnected", 600},
    {GalleryAction::kReconnect, nullptr, 400},
    {GalleryAction::kPairing, "c5_pairing", 600},
    {GalleryAction::kPress, nullptr, 900},  // Cancel has focus: the prompt closes
    {GalleryAction::kStopNav, nullptr, 300},
    {GalleryAction::kSettings, nullptr, 600},
    // Main menu rows from the top: 0 Theme ... 8 Forget Wi-Fi, 9 Forget phones.
    {GalleryAction::kKnob, "c6_forget_phones", 900, 9},
    {GalleryAction::kKnob, nullptr, 300, -9},
    {GalleryAction::kStartNav, nullptr, 300},
}};

struct Gallery {
  const char* dir = nullptr;
  const GalleryStep* steps = kGallerySteps.data();
  size_t step_count = kGallerySteps.size();
  bool started = false;
  size_t theme = 0;
  size_t step = 0;
  uint32_t step_start_ms = 0;
  core::CallPhase call = core::CallPhase::kIdle;
};

void ShowScreen(ui::screen_manager::ScreenId target) {
  for (int i = 0; i < 3 && ui::screen_manager::Current() != target; ++i) {
    ui::screen_manager::NextScreen(app);
  }
}

const GalleryStep& CurrentStep(const Gallery& gallery) {
  return gallery.steps[gallery.step];  // NOLINT(*-pointer-arithmetic)
}

void StartGalleryStep(Gallery& gallery, Tour& tour, uint32_t now) {
  namespace sm = ui::screen_manager;
  switch (CurrentStep(gallery).action) {
    case GalleryAction::kLoadTheme: {
      core::Settings settings = app.State().settings;
      settings.theme = core::ThemeAt(gallery.theme).id;
      app.LoadSettings(settings);
      break;
    }
    case GalleryAction::kNav:
      ShowScreen(sm::ScreenId::kNav);
      break;
    case GalleryAction::kCallRinging:
      gallery.call = core::CallPhase::kRinging;
      break;
    case GalleryAction::kCallActive:
      gallery.call = core::CallPhase::kActive;
      break;
    case GalleryAction::kCallEnd:
      gallery.call = core::CallPhase::kIdle;
      app.SetCall(core::CallPhase::kIdle, "", now);  // --chronos: no ride script resets it
      break;
    case GalleryAction::kMusic:
      ShowScreen(sm::ScreenId::kMusic);
      break;
    case GalleryAction::kSettings:
      ShowScreen(sm::ScreenId::kSettings);
      break;
    case GalleryAction::kThemeList:
      tour.Press(now);
      break;
    case GalleryAction::kLeaveThemeList:
      for (size_t i = 0; i < gallery.theme; ++i) {  // the list opens focused on its first row
        ui::app_shell::OnKnobStep(1);
      }
      tour.Press(now);
      break;
    case GalleryAction::kInputTest:
      sm::OpenInputTest();
      break;
    case GalleryAction::kLeaveInputTest:
      sm::CloseInputTest();
      break;
    case GalleryAction::kSaveWifi:
      wifi_setup.SetSaved("Home Wi-Fi");
      break;
    case GalleryAction::kKnob: {
      const int arg = CurrentStep(gallery).arg;
      for (int i = 0; i < (arg < 0 ? -arg : arg); ++i) {
        ui::app_shell::OnKnobStep(arg < 0 ? -1 : 1);
      }
      break;
    }
    case GalleryAction::kPress:
      tour.Press(now);
      break;
    case GalleryAction::kLongPress:
      tour.Hold(now, kLongHoldMs);
      break;
    case GalleryAction::kManifest:
      fake.manifest_error =
          CurrentStep(gallery).arg == 0 ? core::OtaError::kNone : core::OtaError::kBadSignature;
      break;
    case GalleryAction::kDisconnect:
      ShowScreen(sm::ScreenId::kNav);
      fake_chronos.SetConnected(false, now, app);
      break;
    case GalleryAction::kReconnect:
      fake_chronos.SetConnected(true, now, app);
      break;
    case GalleryAction::kPairing:
      fake_chronos.RequestPairing(708361, now, app);
      break;
    case GalleryAction::kStopNav:
      fake_chronos.SetNavigating(false, now, app);
      break;
    case GalleryAction::kStartNav:
      fake_chronos.SetNavigating(true, now, app);
      break;
    case GalleryAction::kWait:
      break;
  }
}

// Runs after the demo ride's Poll each loop. Returns false when every theme is done.
bool RunGallery(Gallery& gallery, Tour& tour, lv_display_t* display, uint32_t now) {
  if (!gallery.started) {
    if (now - tour.start_ms < kGalleryWarmupMs) {
      return true;
    }
    demo_ride.SetPaused(true);  // stopped: Settings is offered
    gallery.started = true;
    StartGalleryStep(gallery, tour, now);
    gallery.step_start_ms = now;
  }
  if (gallery.call != core::CallPhase::kIdle) {
    app.SetCall(gallery.call, "Demo Caller", now);  // over the demo ride's idle call state
  }

  const GalleryStep& step = CurrentStep(gallery);
  if (now - gallery.step_start_ms < step.settle_ms) {
    return true;
  }
  if (step.name != nullptr) {
    std::array<char, 64> name{};
    std::snprintf(name.data(), name.size(), "%s_%s", core::ThemeAt(gallery.theme).short_name,
                  step.name);
    SaveScreenshot(display, gallery.dir, name.data());
  }
  if (++gallery.step == gallery.step_count) {
    gallery.step = 0;
    if (++gallery.theme == core::ThemeCount()) {
      return false;
    }
  }
  StartGalleryStep(gallery, tour, now);
  gallery.step_start_ms = now;
  return true;
}

}  // namespace

int main(int argc, char** argv) {
  Tour tour;
  Gallery gallery;
  tour.dir = ParseDirOption(argc, argv, "--tour");
  gallery.dir = ParseDirOption(argc, argv, "--gallery");
  const char* shot_dir = tour.dir != nullptr ? tour.dir : gallery.dir;
  if (shot_dir != nullptr) {
    settings_path = std::string(shot_dir) + "/settings.bin";  // never touch the user's file
    std::remove(settings_path.c_str());                       // always start from defaults
  }
  const uint32_t speed = shot_dir != nullptr ? kTourSpeed : ParseSpeed(argc, argv);
  demo_ride.SetSpeedFactor(speed);
  for (int i = 1; i < argc; ++i) {
    chronos_mode =
        chronos_mode || std::strcmp(argv[i], "--chronos") == 0;  // NOLINT(*-pointer-arithmetic)
  }
  if (chronos_mode) {
    source = &fake_chronos;
    gallery.steps = kChronosGallerySteps.data();
    gallery.step_count = kChronosGallerySteps.size();
  }

  lv_init();
  lv_tick_set_cb(SDL_GetTicks);

  lv_display_t* display = lv_sdl_window_create(kSize, kSize);
  if (display == nullptr) {
    std::fprintf(stderr, "sim: could not open the SDL window\n");
    return 1;
  }
  lv_sdl_window_set_title(display, "sārathi sim");
  SDL_AddEventWatch(WatchInput, nullptr);

  lv_indev_t* touch = lv_indev_create();
  lv_indev_set_type(touch, LV_INDEV_TYPE_POINTER);
  lv_indev_set_read_cb(touch, ReadTouch);
  lv_indev_set_display(touch, display);

  LoadSettingsFile();  // before the shell, so the saved theme applies from the first frame
  ui::app_shell::Init(app, input_test, pins::kKnobDebounceMs, pins::kKnobDoubleClickMs, ota_update,
                      wifi_setup, SimRandom);
  ui::lvgl_port::AttachFrameTimer(display, TimeUs);
  CreateBacklightShade();  // under the round mask
  CreateRoundMask();

  std::printf(
      "sārathi sim (ride speed x%u): arrows/wheel = knob, Enter = button (double-click = next "
      "screen, hold = leave input test), mouse = touch, S = pause ride. Close the window to "
      "quit.\n",
      static_cast<unsigned>(speed));
  tour.start_ms = SDL_GetTicks();
  uint32_t last_perf_ms = tour.start_ms;

  for (;;) {
    const uint32_t now = SDL_GetTicks();
    source->Poll(now, app);

    lv_timer_handler();  // renders, reads input, pumps SDL events; exits when the window closes

    if (tour.dir != nullptr && !RunTour(tour, display, now)) {
      return 0;
    }
    if (gallery.dir != nullptr && !RunGallery(gallery, tour, display, now)) {
      return 0;
    }
    if (pause_toggle_requested) {
      pause_toggle_requested = false;
      demo_ride.SetPaused(!demo_ride.Paused());
      std::printf("sim: ride %s\n", demo_ride.Paused() ? "paused" : "resumed");
    }
    while (pending_steps > 0) {
      ui::app_shell::OnKnobStep(1);
      --pending_steps;
    }
    while (pending_steps < 0) {
      ui::app_shell::OnKnobStep(-1);
      ++pending_steps;
    }
    ui::app_shell::OnButtonSample(EnterHeld() || tour.ButtonDown(now), now);
    ui::app_shell::Update(now);
    ShowBacklight(ui::app_shell::BacklightPercent());

    core::Command command;
    while (app.TakeCommand(command)) {
      source->OnCommand(command, now, app);
    }
    RunFakeOta(now);
    RunFakeWifiSetup(now);
    if (settings_saver.ShouldSave(app.SettingsRevision(), now)) {
      SaveSettingsFile();
    }

    if (shot_dir == nullptr && now - last_perf_ms >= kPerfPeriodMs) {
      last_perf_ms = now;
      LogPerf();
    }
    SDL_Delay(kLoopDelayMs);
  }
}
