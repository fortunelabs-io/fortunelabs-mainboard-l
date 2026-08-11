// SPDX-License-Identifier: Apache-2.0

/**
 * @file task_display.c
 * @brief Consumer task that listens for text updates and renders them through
 *        the injected display driver contract.
 */

#include "tasks/task_display.h"
#include "common/app_types.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "TASK_DISPLAY";

void task_display(void *pvParameters) {
    ESP_LOGI(TAG, "Display task started.");

    // pvParameters must be a task_display_ctx_t* set up by main.c. This is
    // the one and only place task_display learns which concrete display it
    // renders to; everything below this line speaks the contract only.
    const task_display_ctx_t *ctx = (const task_display_ctx_t *)pvParameters;
    const display_driver_t   *drv = ctx->driver;

    display_msg_t incoming_msg;

    while (1) {
        if (xQueueReceive(g_queue_display, &incoming_msg, portMAX_DELAY) == pdTRUE) {
            // Render the line through the injected HAL contract, not a named driver
            drv->show_text(incoming_msg.row, incoming_msg.text);
        }
    }
}
