// LVGL's allocator on the device (lv_conf.h: LV_STDLIB_CUSTOM). Everything LVGL allocates
// (objects, styles, draw-time buffers) prefers PSRAM, falling back to internal RAM. With the
// C library allocator, anything under 4 KB landed in internal RAM, which Wi-Fi and a TLS
// session need during an update: a screen change mid-download (an incoming call) could then
// starve them (docs/hardware.md).

#include <esp_heap_caps.h>
#include <lvgl.h>

namespace {

constexpr uint32_t kPsram = MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT;
constexpr uint32_t kInternal = MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;

}  // namespace

// The rest of LVGL's custom-allocator interface (pools, monitor, test) isn't needed.
void lv_mem_init() {}
void lv_mem_deinit() {}
lv_mem_pool_t lv_mem_add_pool(void* /*mem*/, size_t /*bytes*/) { return nullptr; }
void lv_mem_remove_pool(lv_mem_pool_t /*pool*/) {}
void* lv_malloc_core(size_t size) { return heap_caps_malloc_prefer(size, 2, kPsram, kInternal); }
void* lv_realloc_core(void* p, size_t new_size) {
  return heap_caps_realloc_prefer(p, new_size, 2, kPsram, kInternal);
}
void lv_free_core(void* p) { heap_caps_free(p); }
void lv_mem_monitor_core(lv_mem_monitor_t* /*mon_p*/) {}
lv_result_t lv_mem_test_core() { return LV_RESULT_OK; }
