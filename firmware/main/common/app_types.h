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
#include "hal/display_driver.h"
#include "hal/sensor_driver.h"

#ifdef __cplusplus
extern "C" {
#endif
typedef struct {
    uint8_t row;
    // Capacity comes from the display contract, not from a panel. See
    // DISPLAY_TEXT_MAX_LEN in hal/display_driver.h.
    char text[DISPLAY_TEXT_MAX_LEN + 1]; // + null terminator
} display_msg_t;

/* Queue handles, created in main.c */
extern QueueHandle_t g_queue_display;
extern QueueHandle_t g_queue_actuator;

#ifdef __cplusplus
}
#endif
