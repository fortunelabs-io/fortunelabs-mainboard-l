// SPDX-License-Identifier: Apache-2.0

/**
 * @file system_log.c
 * @brief Logging Facade Implementation
 */

#include "system/system_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

static const char *TAG = "system_log";

// One queued log line
typedef struct {
    char   buf[SYSTEM_LOG_LINE_MAX];
    size_t len;
} log_line_t;

static QueueHandle_t               s_queue        = NULL;
static vprintf_like_t              s_orig_vprintf = NULL;  // default UART driver
static volatile system_log_sink_fn s_sink         = NULL;  // remote sink
static volatile bool               s_forwarding   = false; // recursion guard
static bool                        s_initialized  = false;

/* ---------------------------- PRIVATE HELPER -------------------------- */
/**
 * @brief esp_log vprintf hook: write to serial, then queue a copy for the sink.
 *
 * Always writes to serial first via the original vprintf writer, so
 * sink availability never affects serial output. If a sink is
 * registered and the forwarder is not already mid-delivery, formats a
 * second copy of the line into the queue for task_log_forwarder to
 * pick up; the queue send is non-blocking and drops the line on full
 * so logging never stalls the calling task.
 *
 * @param fmt   printf-style format string
 * @param args  Format arguments
 *
 * @return Number of characters written to serial, as returned by the underlying vprintf
 */
static int log_vprintf(const char *fmt, va_list args) {
    va_list args_copy;
    va_copy(args_copy, args);

    int n = s_orig_vprintf ? s_orig_vprintf(fmt, args) : vprintf(fmt, args);

    if (s_sink != NULL && s_queue != NULL && !s_forwarding) {
        log_line_t line;
        int        written = vsnprintf(line.buf, sizeof(line.buf), fmt, args_copy);
        if (written > 0) {
            line.len = (written < (int)sizeof(line.buf)) ? (size_t)written : sizeof(line.buf) - 1;
            // Non-blocking: drop on full so logging never stalls a task
            (void)xQueueSend(s_queue, &line, 0);
        }
    }
    va_end(args_copy);
    return n;
}

/**
 * @brief Drain the log queue and deliver each line to the registered sink.
 *
 * portMAX_DELAY here is correct: this task has no watchdog obligation
 * and is meant to block indefinitely until a line is queued.
 *
 * @param pvParameters  Unused, required by the FreeRTOS task signature
 *
 * @return void (never returns; runs for the lifetime of the task)
 */
static void task_log_forwarder(void *pvParameters) {
    (void)pvParameters;
    log_line_t line;

    while (1) {
        if (xQueueReceive(s_queue, &line, portMAX_DELAY) == pdTRUE) {
            system_log_sink_fn sink = s_sink;
            if (sink != NULL) {
                s_forwarding = true;
                sink(line.buf, line.len);
                s_forwarding = false;
            }
        }
    }
}

/* ---------------------------- LIFECYCLE -------------------------- */
esp_err_t system_log_init(esp_log_level_t default_level) {
    esp_log_level_set("*", default_level);

    s_queue = xQueueCreate(SYSTEM_LOG_QUEUE_DEPTH, sizeof(log_line_t));
    if (s_queue == NULL) {
        ESP_LOGE(TAG, "Failed to create log queue");
        return ESP_ERR_NO_MEM;
    }

    BaseType_t ok = xTaskCreate(task_log_forwarder, "log_fwd", 4096, NULL, 1, NULL);
    if (ok != pdPASS) {
        ESP_LOGE(TAG, "Failed to create forwarder task");
        vQueueDelete(s_queue);
        s_queue = NULL;
        return ESP_ERR_NO_MEM;
    }

    // Install the hook last; save the default writer to chain serial output
    s_orig_vprintf = esp_log_set_vprintf(log_vprintf);
    s_initialized  = true;

    ESP_LOGI(TAG, "Logging ready (level=%d, sink idle)", default_level);
    return ESP_OK;
}

/* ---------------------------- LEVEL CONTROL -------------------------- */
void system_log_set_level(const char *tag, esp_log_level_t level) {
    esp_log_level_set(tag ? tag : "*", level);
}

/* ---------------------------- SINK MANAGEMENT -------------------------- */
esp_err_t system_log_register_sink(system_log_sink_fn sink) {
    if (!s_initialized)
        return ESP_ERR_INVALID_STATE;

    s_sink = sink;
    ESP_LOGI(TAG, "Remote sink %s", sink ? "registered" : "cleared");
    return ESP_OK;
}

void system_log_unregister_sink(void) { s_sink = NULL; }