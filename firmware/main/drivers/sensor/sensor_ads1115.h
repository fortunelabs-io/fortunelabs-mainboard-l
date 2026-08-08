// SPDX-License-Identifier: Apache-2.0
/**
 * @file sensor_ads1115.h
 * @brief ADS1115 Sensor HAL Adapter Interface
 *
 * Binds the concrete ADS1115 ADC driver (drivers/ic/ads1115) to the
 * generic sensor_driver_t contract, so task_sensor can read a real
 * analog channel through the same interface it uses for the dummy
 * potentiometer. Swapping the two is a one-line change in main.c.
 *
 * This adapter converts the driver's raw 16-bit conversion result into
 * a voltage in the sensor_reading_t.value float, using the per-channel
 * PGA setting to derive the LSB size.
 *
 * Singleton constraint: sensor_driver_t methods carry no instance
 * pointer, so this adapter, like sensor_dummy, backs exactly one active
 * device in file-static state. One adapter instance reads one channel,
 * fixed at init. Reading several channels concurrently is outside the
 * shape of this contract and is not supported here.
 *
 * Wiring: the caller passes ADS1115-specific setup through the generic
 * sensor_config_t as follows:
 *   - bus       -> the initialized i2c_bus_t
 *   - i2c_addr  -> the ADS1115 address strap (e.g. ADS1115_ADDR_GND)
 *   - channel   -> the active logical channel to read, 0..3
 *   - extra     -> pointer to a sensor_ads1115_config_t (see below)
 */

#pragma once

#include "drivers/ic/ads1115.h"
#include "hal/sensor_driver.h"

#ifdef __cplusplus
extern "C" {
#endif

/* --------------------------- CONFIG STRUCT ----------------------------*/
/**
 * @brief ADS1115-specific init payload, passed via sensor_config_t.extra.
 *
 * channel_config[i] must describe logical channel i. The adapter reads
 * only the channel named by sensor_config_t.channel, but the full table
 * is forwarded to the underlying driver at init.
 *
 * @param channel_config  Per-channel PGA and data-rate settings
 */
typedef struct {
    ads1115_channel_config_t channel_config[4];
} sensor_ads1115_config_t;

/* --------------------------- DRIVER INSTANCE ----------------------------*/
/**
 * @brief Exported instance of the ADS1115 sensor adapter.
 */
extern const sensor_driver_t sensor_ads1115_driver;

#ifdef __cplusplus
}
#endif