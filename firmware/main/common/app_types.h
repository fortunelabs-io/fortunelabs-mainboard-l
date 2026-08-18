// SPDX-License-Identifier: Apache-2.0
/**
 * @brief Shared application types and global queue handles.
 *
 * This file is the "glue" between tasks. Every task includes this
 * to know the shape of data flowing through queues and which
 * queues exist.
 *
 * sensor_reading_t is defined in hal/sensor_driver.h (single source
 * of truth) and re-exported here for convenience.
 */
#pragma once

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "hal/sensor_driver.h"

#ifdef __cplusplus
extern "C" {
#endif
typedef struct {
    uint8_t row;
    char    text[17]; // 16 characters + 1 null terminator for SSD1306
} display_msg_t;

/* Queue handles, created in main.c */
extern QueueHandle_t g_queue_display;
extern QueueHandle_t g_queue_actuator;

/* Latest sensor sample, for consumers that want the current value rather
 * than every value: depth 1, written with xQueueOverwrite by task_sensor and
 * read with xQueuePeek by telemetry. A slow reader sees the newest sample
 * instead of a backlog, and the sample stays readable after a peek.
 */
extern QueueHandle_t g_queue_comm;

#ifdef __cplusplus
}
#endif
