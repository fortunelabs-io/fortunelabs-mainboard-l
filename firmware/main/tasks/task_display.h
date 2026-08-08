// SPDX-License-Identifier: Apache-2.0

/**
 * @file task_display.h
 * @brief Public interface for task_display: injected context and entry point.
 *
 * task_display depends only on the display_driver_t contract. It never
 * includes a concrete driver header.
 */

#pragma once

#include "hal/display_driver.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Context injected into task_display via pvParameters.
 *
 * @param driver  Pointer to the display driver contract this task consumes.
 *                Owned by main.c; must outlive the task.
 */
typedef struct {
    const display_driver_t *driver;
} task_display_ctx_t;

/**
 * @brief FreeRTOS entry point. pvParameters must be a task_display_ctx_t*.
 */
void task_display(void *pvParameters);

#ifdef __cplusplus
}
#endif
