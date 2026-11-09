// Map renderer bench on the device (Phase 4 step G1, ADR-0009 "Spike"): `pio run -e
// crowpanel21-mapbench -t upload`, then read the serial log. Replaces the app until the app is
// flashed again. Every 5 s it logs the phase, frames, map draw time, frame time, flush time, the
// segments drawn and memory. Watch the panel for flicker, line shift or garbling.

#include <cinttypes>

#include <Arduino.h>
#include <esp32-hal-log.h>
#include <esp_heap_caps.h>
#include <esp_timer.h>

#include "backlight.h"
#include "board_pins.h"
#include "display.h"
#include "frame_stats.h"
#include "lvgl_port.h"
#include "map_bench_scene.h"
#include "theme_binding.h"
#include "theme_registry.h"

namespace board = sarathi::board;
namespace core = sarathi::core;
namespace ui = sarathi::ui;
namespace pins = sarathi::board::pins;

// The recorded chunk sets, generated at build time by bench/embed_chunks.py.
extern const sarathi::bench::ChunkBlob kBenchChunks[];  // NOLINT(*-avoid-c-arrays)
extern const size_t kBenchChunkCount;

namespace {

constexpr const char* kTag = "bench";
constexpr uint32_t kPerfPeriodMs = 5000;
constexpr size_t kArenaBytes = 1536 * 1024;  // ADR-0009: 1.5 MB of PSRAM for chunks
constexpr uint32_t kSerialWaitMs = 2000;
constexpr uint32_t kSerialTxTimeoutMs = 1;
constexpr size_t kSerialTxBufferBytes = 2048;
constexpr uint32_t kKiB = 1024;

// NOLINTBEGIN(cppcoreguidelines-avoid-non-const-global-variables)
board::Backlight backlight;
board::Display display;
core::FrameStats flush_stats;
sarathi::bench::MapBenchScene* scene = nullptr;
bool ready = false;
uint32_t last_perf_ms = 0;
// NOLINTEND(cppcoreguidelines-avoid-non-const-global-variables)

uint32_t TimeUs() { return static_cast<uint32_t>(esp_timer_get_time()); }
uint32_t TickMs() { return millis(); }
void* AllocPsram(size_t bytes) { return heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM); }
void* AllocInternal(size_t bytes) { return heap_caps_malloc(bytes, MALLOC_CAP_INTERNAL); }

// LVGL's draw buffers: one buffer of SARATHI_BENCH_STRIP_ROWS rows in internal RAM (40, the
// fastest that fits), or 0 for two full-screen buffers in PSRAM like the app today. Compare with
// PLATFORMIO_BUILD_FLAGS="-DSARATHI_BENCH_STRIP_ROWS=0" pio run -e crowpanel21-mapbench -t upload
#ifndef SARATHI_BENCH_STRIP_ROWS
#define SARATHI_BENCH_STRIP_ROWS 40
#endif
constexpr int16_t kStripRows = SARATHI_BENCH_STRIP_ROWS;
bool NoTouch(int16_t& /*x*/, int16_t& /*y*/) { return false; }

void FlushToDisplay(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t* pixels) {
  const uint32_t start = TimeUs();
  display.DrawBitmap(x, y, w, h, pixels);
  flush_stats.Add(TimeUs() - start);
}

void LogPerf() {
  const core::FrameStats::Summary frames = ui::lvgl_port::TakeFrameStats();
  const core::FrameStats::Summary flushes = flush_stats.TakeSummary();
  const ui::MapView::Stats map = scene->TakeStats();
  constexpr float kMs = 1000.0F;
  const float n = frames.count > 0 ? static_cast<float>(frames.count) : 1.0F;
  const auto per_frame = [&](uint32_t us) {
    return static_cast<double>(static_cast<float>(us) / n / kMs);
  };
  ESP_LOGI(kTag,
           "bench %s | fps %.1f | frame avg %.1f ms max %.1f ms | map per frame: prepare %.1f bg "
           "%.1f polys %.1f lines %.1f ms (%u strips) | flush per frame %.1f ms (%u) | chunks %u "
           "shapes %u segments %u%s | store %u chunks %u KB | psram %u KB free | heap int %u KB "
           "free (largest %u)",
           scene->PhaseName(), static_cast<double>(n * kMs / kPerfPeriodMs),
           static_cast<double>(static_cast<float>(frames.avg_us) / kMs),
           static_cast<double>(static_cast<float>(frames.max_us) / kMs),
           per_frame(map.last.prepare_us), per_frame(map.last.background_us),
           per_frame(map.last.polygons_us), per_frame(map.last.lines_us),
           static_cast<unsigned>(static_cast<float>(map.draws) / n + 0.5F),
           static_cast<double>(static_cast<float>(flushes.avg_us) *
                               static_cast<float>(flushes.count) / n / kMs),
           static_cast<unsigned>(flushes.count), static_cast<unsigned>(map.last.chunks),
           static_cast<unsigned>(map.last.shapes), static_cast<unsigned>(map.last.segments),
           map.last.truncated ? " TRUNCATED" : "", static_cast<unsigned>(scene->Store().Count()),
           static_cast<unsigned>(scene->Store().UsedBytes() / kKiB),
           static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / kKiB),
           static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / kKiB),
           static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL) / kKiB));
}

}  // namespace

void setup() {
  Serial.setTxBufferSize(kSerialTxBufferBytes);
  Serial.begin(115200);
  Serial.setTxTimeoutMs(kSerialTxTimeoutMs);
  Serial.setDebugOutput(true);
  const uint32_t start = millis();
  while (!Serial && millis() - start < kSerialWaitMs) {
    delay(10);
  }
  if (!display.Begin(backlight, pins::kBacklightDefaultPercent)) {
    ESP_LOGE(kTag, "display init failed");
    return;
  }
  if (!ui::lvgl_port::Init(pins::kScreenWidth, pins::kScreenHeight, FlushToDisplay, NoTouch, TickMs,
                           kStripRows > 0 ? AllocInternal : AllocPsram, kStripRows)) {
    ESP_LOGE(kTag, "LVGL buffers failed");
    return;
  }
  ui::lvgl_port::AttachFrameTimer(lv_display_get_default(), TimeUs);
  ui::theme::SetPanelSwapsRedBlue(pins::kSwapRedBlue);
  ui::theme::Apply(core::ThemeFor(core::ThemeId::kClassic));
  auto* arena = static_cast<uint8_t*>(AllocPsram(kArenaBytes));
  auto* scratch = static_cast<uint8_t*>(AllocPsram(sarathi::bench::MapBenchScene::kScratchBytes));
  auto* scene_mem = AllocPsram(sizeof(sarathi::bench::MapBenchScene));
  if (arena == nullptr || scratch == nullptr || scene_mem == nullptr) {
    ESP_LOGE(kTag, "PSRAM for the bench failed");
    return;
  }
  scene = new (scene_mem) sarathi::bench::MapBenchScene();
  const uint32_t load_start = TimeUs();
  const size_t stored =
      scene->Init(arena, kArenaBytes, scratch, kBenchChunks, kBenchChunkCount, TimeUs);
  ESP_LOGI(kTag, "draw buffers: %d rows (%s)", kStripRows,
           kStripRows > 0 ? "strips in internal RAM" : "full screen in PSRAM");
  ESP_LOGI(kTag, "loaded %u of %u chunks (%u KB) in %.1f ms; phases of %u s",
           static_cast<unsigned>(stored), static_cast<unsigned>(kBenchChunkCount),
           static_cast<unsigned>(scene->Store().UsedBytes() / kKiB),
           static_cast<double>(static_cast<float>(TimeUs() - load_start) / 1000.0F),
           static_cast<unsigned>(sarathi::bench::MapBenchScene::kPhaseMs / 1000));
  ready = true;
}

void loop() {
  if (!ready) {
    delay(100);
    return;
  }
  const uint32_t now = millis();
  scene->Step(now);
  ui::lvgl_port::Run();
  if (now - last_perf_ms >= kPerfPeriodMs) {
    last_perf_ms = now;
    LogPerf();
  }
  delay(1);
}
