// SPDX-License-Identifier: Apache-2.0

/**
 * @file task_status.c
 * @brief Consumer task that reports firmware liveness on the status LED.
 *
 * Blinks a short flash every STATUS_HEARTBEAT_PERIOD_MS, forever.
 *
 * The heartbeat starting at all is the signal that the flash succeeded: this
 * task is spawned last by app_main, so it only ever runs once every driver
 * init returned ESP_OK and the scheduler reached it. A board that flashes but
 * faults during init stays dark; one that booted and later wedged stops
 * blinking. No pattern to decode - LED pulsing means the firmware is alive.
 *
 * Deliberately separate from the supervisor's MQTT heartbeat: this one needs
 * no network, no broker, and no I2C, so it still reports when those are the
 * thing that is broken.
 */

#include "tasks/task_status.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define STATUS_HEARTBEAT_ON_MS 60
#define STATUS_HEARTBEAT_PERIOD_MS 2000

static const char *TAG = "task_status";

void task_status(void *pvParameters) {
    // pvParameters must be a task_status_ctx_t* set up by main.c. This is the
    // one and only place task_status learns which concrete driver it is
    // driving; everything below this line speaks the contract only.
    const task_status_ctx_t *ctx = (const task_status_ctx_t *)pvParameters;
    const output_driver_t   *drv = ctx->driver;

    ESP_LOGI(TAG, "Boot confirmed. Heartbeat running on %s.", drv->name);

    // Anchor the period to a fixed wake time so the flash does not drift by
    // STATUS_HEARTBEAT_ON_MS on every cycle.
    TickType_t last_wake = xTaskGetTickCount();

    while (1) {
        drv->set(0, true);
        vTaskDelay(pdMS_TO_TICKS(STATUS_HEARTBEAT_ON_MS));
        drv->set(0, false);

        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(STATUS_HEARTBEAT_PERIOD_MS));
    }
}
