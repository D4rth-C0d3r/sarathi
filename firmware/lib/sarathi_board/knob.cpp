// Adapted from Elecrow's CrowPanel 2.1" examples: the sampling task from
// RotaryScreen_2_1.ino (encTaskSafe) and the decoder from Lesson02-Rotate.ino.

#include "knob.h"

#include <Arduino.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>

#include "board_pins.h"
#include "expander.h"
#include "knob_decoder.h"

namespace sarathi::board {
namespace {

namespace p = pins;

constexpr const char* kTag = "knob";

QueueHandle_t step_queue = nullptr;  // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)

void SampleTask(void* /*unused*/) {
  core::KnobDecoder decoder(p::kKnobTransitionsPerDetent);
  decoder.Reset(digitalRead(p::kKnobA) == HIGH, digitalRead(p::kKnobB) == HIGH);
  for (;;) {
    const int8_t step =
        decoder.Update(digitalRead(p::kKnobA) == HIGH, digitalRead(p::kKnobB) == HIGH);
    if (step != 0) {
      xQueueSend(step_queue, &step, 0);  // drop the step if the UI is far behind
    }
    vTaskDelay(pdMS_TO_TICKS(p::kKnobPollMs));
  }
}

}  // namespace

bool Knob::Begin() {
  pinMode(p::kKnobA, INPUT);  // external 10k pull-ups on the board
  pinMode(p::kKnobB, INPUT);

  step_queue = xQueueCreate(p::kKnobQueueLength, sizeof(int8_t));
  if (step_queue == nullptr) {
    ESP_LOGE(kTag, "queue alloc failed");
    return false;
  }
  if (xTaskCreatePinnedToCore(SampleTask, "knob", p::kKnobTaskStack, nullptr, p::kKnobTaskPriority,
                              nullptr, p::kKnobTaskCore) != pdPASS) {
    ESP_LOGE(kTag, "task create failed");
    return false;
  }
  ESP_LOGI(kTag, "ready");
  return true;
}

bool Knob::TakeStep(int8_t& step) {
  return step_queue != nullptr && xQueueReceive(step_queue, &step, 0) == pdTRUE;
}

bool Knob::ReadButtonRaw() {
  return !expander::Read(p::kPcfKnobButton);  // active low
}

}  // namespace sarathi::board
