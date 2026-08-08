// SPDX-License-Identifier: Apache-2.0

/**
 * @file task_actuator.c
 * @brief Consumer task that drives the physical/dummy LED based on queue commands.
 */

#include "tasks/task_actuator.h"
#include "common/app_types.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "task_actuator";

void task_actuator(void *pvParameters) {
    ESP_LOGI(TAG, "Actuator task started.");

    // pvParameters must be a task_actuator_ctx_t* set up by main.c. This is
    // the one and only place task_actuator learns which concrete driver it
    // is driving; everything below this line speaks the contract only.
    const task_actuator_ctx_t *ctx = (const task_actuator_ctx_t *)pvParameters;
    const output_driver_t     *drv = ctx->driver;

    bool target_led_state = false;

    while (1) {
        // Block until a new command is received to change the LED state
        if (xQueueReceive(g_queue_actuator, &target_led_state, portMAX_DELAY) == pdTRUE) {
            // Drive the output through the injected HAL contract, not a named driver
            drv->set(0, target_led_state);
            ESP_LOGD(TAG, "Hardware LED synchronized to: %s", target_led_state ? "ON" : "OFF");
        }
    }
}