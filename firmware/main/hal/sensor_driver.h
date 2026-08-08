// SPDX-License-Identifier: Apache-2.0

/**
 * @file sensor_driver.h
 * @brief Sensor HAL Contract
 *
 * Any sensor (potentiometer, ADS1115, SHT31, MQ-137, or a future
 * daughter board sensor) implements this interface.
 *
 * The task layer (task_sensor.c) only depends on this contract, never
 * on a specific sensor IC. Swapping sensors means swapping the driver
 * pointer in main.c, with zero changes to business logic.
 *
 * Design note: value is deliberately a generic float. What it represents
 * (voltage, temperature, ppm) depends on the concrete driver.
 */

#pragma once

#include "bus/i2c_bus.h"
#include "esp_err.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* --------------------------- CONFIG STRUCT ----------------------------*/
/**
 * @brief Configuration structure for initializing a sensor driver instance.
 *
 * @param bus       Pointer to the initialized I2C bus instance
 * @param i2c_addr  7-bit I2C address of the target sensor device
 * @param channel   Default hardware channel or sub-sensor pin configuration
 * @param extra     Generic pointer to a custom, driver-specific configuration
 */
typedef struct {
    i2c_bus_t *bus;
    uint8_t    i2c_addr;
    uint8_t    channel;
    void      *extra;
} sensor_config_t;

/* --------------------------- READING STRUCT ----------------------------*/
/**
 * @brief One telemetry sample produced by a sensor driver's read().
 *
 * @param channel       Hardware channel or sub-sensor this reading came from
 * @param value         Sample value; unit depends on the concrete driver
 * @param timestamp_ms  Time the sample was taken, in milliseconds since boot
 */
typedef struct {
    uint8_t  channel;
    float    value;
    uint32_t timestamp_ms;
} sensor_reading_t;

/**
 * @brief Sensor driver interface (vtable).
 * Defines the unified contract for all sensor operations. Single access
 * path: the task layer only ever reaches a concrete sensor implementation
 * through these function pointers.
 *
 * @param cfg  Pointer to the sensor_config_t structure
 * @param out  Pointer to the sensor_reading_t structure where the sample is stored
 *
 * @return
 * - ESP_OK   : Operation completed successfully
 * - ESP_FAIL / Specific esp_err_t : Communication error, timeout, or sensor read failure
 * - void     : No return value
 */
typedef struct {
    const char *name;

    /**
     * @brief Initialize the sensor hardware
     */
    esp_err_t (*init)(const sensor_config_t *cfg);
    /**
     * @brief Take one sample
     */
    esp_err_t (*read)(sensor_reading_t *out);
    /**
     * @brief Release hardware resources (nullable)
     */
    void (*deinit)(void);
} sensor_driver_t;

#ifdef __cplusplus
}
#endif