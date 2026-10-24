// LVGL's allocator in the simulator (lv_conf.h: LV_STDLIB_CUSTOM): plain malloc. The device
// version (sarathi_board/lvgl_memory.cpp) prefers PSRAM.

#include <cstdlib>

#include <lvgl.h>

void lv_mem_init() {}
void lv_mem_deinit() {}
lv_mem_pool_t lv_mem_add_pool(void* /*mem*/, size_t /*bytes*/) { return nullptr; }
void lv_mem_remove_pool(lv_mem_pool_t /*pool*/) {}
void* lv_malloc_core(size_t size) { return std::malloc(size); }  // NOLINT(*-no-malloc)
void* lv_realloc_core(void* p, size_t new_size) {
  return std::realloc(p, new_size);  // NOLINT(*-no-malloc)
}
void lv_free_core(void* p) { std::free(p); }  // NOLINT(*-no-malloc)
void lv_mem_monitor_core(lv_mem_monitor_t* /*mon_p*/) {}
lv_result_t lv_mem_test_core() { return LV_RESULT_OK; }
