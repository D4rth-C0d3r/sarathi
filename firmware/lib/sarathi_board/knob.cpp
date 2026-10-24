// Adapted from Elecrow's CrowPanel 2.1" examples: the sampling task from
// RotaryScreen_2_1.ino (encTaskSafe) and the decoder from Lesson02-Rotate.ino.

#include "knob.h"

#include <atomic>

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

// NOLINTBEGIN(cppcoreguidelines-avoid-non-const-global-variables)
QueueHandle_t step_queue = nullptr;
QueueHandle_t button_queue = nullptr;
TaskHandle_t sample_task = nullptr;
std::atomic<bool> button_pressed{false};
// NOLINTEND(cppcoreguidelines-avoid-non-const-global-variables)

void SampleTask(void* /*unused*/) {
  core::KnobDecoder decoder(p::kKnobTransitionsPerDetent);
  decoder.Reset(digitalRead(p::kKnobA) == HIGH, digitalRead(p::kKnobB) == HIGH);
  uint32_t polls = 0;
  for (;;) {
    const int8_t step =
        decoder.Update(digitalRead(p::kKnobA) == HIGH, digitalRead(p::kKnobB) == HIGH);
    if (step != 0) {
      xQueueSend(step_queue, &step, 0);  // drop the step if the UI is far behind
    }
    if (++polls % p::kKnobButtonPollEvery == 0) {
      const bool pressed = !expander::Read(p::kPcfKnobButton);  // active low
      const bool was = button_pressed.load();
      if (pressed != was) {
        const auto now = static_cast<uint32_t>(millis());
        const Knob::ButtonSample before{was, now};
        const Knob::ButtonSample after{pressed, now};
        xQueueSend(button_queue, &before, 0);
        xQueueSend(button_queue, &after, 0);
        button_pressed.store(pressed);
      }
    }
    vTaskDelay(pdMS_TO_TICKS(p::kKnobPollMs));
  }
}

}  // namespace

bool Knob::Begin() {
  pinMode(p::kKnobA, INPUT);  // external 10k pull-ups on the board
  pinMode(p::kKnobB, INPUT);

  step_queue = xQueueCreate(p::kKnobQueueLength, sizeof(int8_t));
  button_queue = xQueueCreate(p::kKnobQueueLength, sizeof(ButtonSample));
  button_pressed.store(!expander::Read(p::kPcfKnobButton));  // before the task starts
  if (step_queue == nullptr || button_queue == nullptr) {
    ESP_LOGE(kTag, "queue alloc failed");
    return false;
  }
  if (xTaskCreatePinnedToCore(SampleTask, "knob", p::kKnobTaskStack, nullptr, p::kKnobTaskPriority,
                              &sample_task, p::kKnobTaskCore) != pdPASS) {
    ESP_LOGE(kTag, "task create failed");
    return false;
  }
  ESP_LOGI(kTag, "ready");
  return true;
}

bool Knob::TakeStep(int8_t& step) {
  return step_queue != nullptr && xQueueReceive(step_queue, &step, 0) == pdTRUE;
}

bool Knob::TakeButtonSample(ButtonSample& sample) {
  return button_queue != nullptr && xQueueReceive(button_queue, &sample, 0) == pdTRUE;
}

bool Knob::ButtonPressed() const { return button_pressed.load(); }

uint32_t Knob::StackFreeBytes() const {
  // ESP-IDF counts stack in bytes, so the high-water mark is bytes never used.
  return sample_task != nullptr ? uxTaskGetStackHighWaterMark(sample_task) : 0;
}

}  // namespace sarathi::board
