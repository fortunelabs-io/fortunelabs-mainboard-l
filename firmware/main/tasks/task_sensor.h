// SPDX-License-Identifier: Apache-2.0

/**
 * @file task_sensor.h
 * @brief Public interface for task_sensor: injected context and entry point.
 *
 * task_sensor depends only on the sensor_driver_t contract. It never
 * includes a concrete driver header.
 */

#pragma once

#include "hal/sensor_driver.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Context injected into task_sensor via pvParameters.
 *
 * @param driver  Pointer to the sensor driver contract this task consumes.
 *                Owned by main.c; must outlive the task.
 */
typedef struct {
    const sensor_driver_t *driver;
} task_sensor_ctx_t;

/**
 * @brief FreeRTOS entry point. pvParameters must be a task_sensor_ctx_t*.
 */
void task_sensor(void *pvParameters);

/**
 * @brief Latest voltage sample observed by task_sensor, for other
 *        tasks or diagnostics that need a non-blocking read.
 */
float task_sensor_get_latest_voltage(void);

#ifdef __cplusplus
}
#endif