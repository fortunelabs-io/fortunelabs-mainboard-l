// SPDX-License-Identifier: Apache-2.0

/**
 * @file task_status.h
 * @brief Public interface for task_status: injected context and entry point.
 *
 * task_status depends only on the output_driver_t contract. It never includes
 * a concrete driver header.
 */

#pragma once

#include "hal/output_driver.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Context injected into task_status via pvParameters.
 *
 * @param driver  Pointer to the output driver contract this task consumes.
 *                Owned by main.c; must outlive the task.
 */
typedef struct {
    const output_driver_t *driver;
} task_status_ctx_t;

/**
 * @brief FreeRTOS entry point. pvParameters must be a task_status_ctx_t*.
 */
void task_status(void *pvParameters);

#ifdef __cplusplus
}
#endif
