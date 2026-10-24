// Owns the screens and their focus groups, decides which one is shown, and points the knob
// (LVGL encoder) at the shown screen's group.
//
//   Nav -> Music -> Settings -> Nav ...   double-click; Settings is skipped while moving
//   Call                                  shown while a call rings or is active, then back
//   Pairing                               shown while a phone asks to pair, then back
//   Input test                            from Settings; hold the button to leave
//   Wi-Fi setup, Software update          from Settings; click and long press go to them

#pragma once

#include <cstdint>

#include <lvgl.h>

#include "app_model.h"
#include "input_test_model.h"
#include "ota_update.h"
#include "wifi_setup.h"

namespace sarathi::ui::screen_manager {

enum class ScreenId : uint8_t {
  kNav,
  kMusic,
  kSettings,
  kCall,
  kInputTest,
  kWifiSetup,
  kUpdate,
  kPairing,
};

// Builds every screen once and shows Nav. `random` is a cryptographic random source, for the
// Wi-Fi setup password (esp_random on the device).
void Init(lv_indev_t* encoder, core::AppModel& model, core::OtaUpdate& ota,
          core::WifiSetup& wifi_setup, uint32_t (*random)());

// Shows the call screen when needed and refreshes the shown screen.
void Update(const core::AppModel& model, const core::InputTestState& input_test, uint32_t now_ms);

// Double-click: the next screen in the carousel. Ignored on the call, pairing, input test,
// Wi-Fi setup and update screens.
void NextScreen(const core::AppModel& model);

void OpenInputTest();
void CloseInputTest();

// True on screens that take click and long press themselves (Wi-Fi setup, update).
[[nodiscard]] bool ModalOpen();
void ModalClick();
void ModalLongPress();

[[nodiscard]] ScreenId Current();

}  // namespace sarathi::ui::screen_manager
