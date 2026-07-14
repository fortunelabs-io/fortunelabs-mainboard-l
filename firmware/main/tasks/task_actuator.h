/**
 * @file task_actuator.h
 * @brief Public interface for task_actuator: injected context and entry point.
 *
 * task_actuator depends only on the output_driver_t contract. It never
 * includes a concrete driver header.
 */

#pragma once

#include "hal/output_driver.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Context injected into task_actuator via pvParameters.
 *
 * @param driver  Pointer to the output driver contract this task consumes.
 *                Owned by main.c; must outlive the task.
 */
typedef struct {
    const output_driver_t *driver;
} task_actuator_ctx_t;

/**
 * @brief FreeRTOS entry point. pvParameters must be a task_actuator_ctx_t*.
 */
void task_actuator(void *pvParameters);

#ifdef __cplusplus
}
#endif