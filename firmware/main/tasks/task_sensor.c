// SPDX-License-Identifier: Apache-2.0

/**
 * @file task_sensor.c
 * @brief Periodically samples the injected sensor driver and dispatches state updates.
 */

#include "tasks/task_sensor.h"
#include "common/app_types.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdio.h>

#define SENSOR_PERIOD_MS 200
#define VOLTAGE_THRESHOLD 2.5f

static const char *TAG = "task_sensor";

void task_sensor(void *pvParameters) {
    ESP_LOGI(TAG, "Sensor task started.");

    // pvParameters must be a task_sensor_ctx_t* set up by main.c. This is
    // the one and only place task_sensor learns which concrete driver it
    // is reading from; everything below this line speaks the contract only.
    const task_sensor_ctx_t *ctx = (const task_sensor_ctx_t *)pvParameters;
    const sensor_driver_t   *drv = ctx->driver;

    sensor_reading_t reading;
    bool             last_led_state = false;

    // Main loop: Read sensor, send updates to display and actuator tasks
    while (1) {
        // 1. Read data through the injected HAL contract, not a named driver
        if (drv->read(&reading) == ESP_OK) {
            float current_volt      = reading.value;
            bool  current_led_state = (current_volt > VOLTAGE_THRESHOLD);

            // 2. Publish the sample for consumers that want the current value
            // (telemetry). Overwrite rather than send: the mailbox is depth 1
            // and a reader wants the newest sample, not a queued older one.
            xQueueOverwrite(g_queue_comm, &reading);

            // 3. Send voltage reading to display task via queue (row 0)
            display_msg_t msg_volt;
            msg_volt.row = 0;
            snprintf(msg_volt.text, sizeof(msg_volt.text), "VOLT: %.2f V", current_volt);
            xQueueSend(g_queue_display, &msg_volt, 0);

            // 4. Evaluate threshold & send status to actuator + display queue (row 1)
            if (current_led_state != last_led_state) {
                last_led_state = current_led_state;

                // Send command to actuator task
                xQueueSend(g_queue_actuator, &current_led_state, 0);

                // Send text notification to display task
                display_msg_t msg_status;
                msg_status.row = 2; // Place on row 2 to create a space on the OLED
                if (current_led_state) {
                    snprintf(msg_status.text, sizeof(msg_status.text), "OVER THRESHOLD!");
                } else {
                    snprintf(msg_status.text, sizeof(msg_status.text), "SYSTEM OK");
                }
                xQueueSend(g_queue_display, &msg_status, 0);
            }
        }

        vTaskDelay(pdMS_TO_TICKS(SENSOR_PERIOD_MS));
    }
}