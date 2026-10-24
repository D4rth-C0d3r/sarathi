#include "boot_confirm.h"

#include <esp32-hal-log.h>  // ESP_LOGx through Arduino's logger (CLAUDE.md)
#include <esp_ota_ops.h>

// Overrides the Arduino core's weak default (cores/esp32/esp32-hal-misc.c), which marks a
// PENDING_VERIFY image valid inside initArduino(), before setup() runs. Returning true leaves
// that to ConfirmBoot(), once the UI is up (core::DecideBoot). The core is C, so this needs
// C linkage; a C++ (mangled) definition would silently not override it.
extern "C" bool verifyRollbackLater() { return true; }  // NOLINT(misc-use-internal-linkage)

namespace sarathi::ota {
namespace {

constexpr const char* kTag = "ota_boot";

}  // namespace

BootState ReadBootState() {
  BootState state;
  esp_ota_img_states_t img = ESP_OTA_IMG_UNDEFINED;
  const esp_partition_t* running = esp_ota_get_running_partition();
  if (running != nullptr && esp_ota_get_state_partition(running, &img) == ESP_OK) {
    state.pending_verify = img == ESP_OTA_IMG_PENDING_VERIFY;
  }
  const esp_partition_t* invalid = esp_ota_get_last_invalid_partition();
  state.rolled_back = invalid != nullptr;
  state.invalid_address = invalid != nullptr ? invalid->address : 0;
  ESP_LOGI(kTag, "running %s, %s%s", running != nullptr ? running->label : "?",
           state.pending_verify ? "pending verify" : "confirmed",
           state.rolled_back ? ", an earlier update was rolled back" : "");
  return state;
}

bool ConfirmBoot() {
  const esp_err_t err = esp_ota_mark_app_valid_cancel_rollback();
  if (err == ESP_OK) {
    ESP_LOGI(kTag, "image confirmed");
  } else {
    ESP_LOGE(kTag, "could not confirm the image: %s", esp_err_to_name(err));
  }
  return err == ESP_OK;
}

void RollBack() {
  ESP_LOGW(kTag, "self-check failed: rolling back to the previous image");
  const esp_err_t err = esp_ota_mark_app_invalid_rollback_and_reboot();
  ESP_LOGE(kTag, "could not roll back: %s", esp_err_to_name(err));
}

}  // namespace sarathi::ota
