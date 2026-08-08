// SPDX-License-Identifier: Apache-2.0

/**
 * @file output_driver.h
 * @brief Output HAL Contract
 *
 * Hardware abstraction layer contract for outputs driven from this
 * mainboard. This prevents coupling between business logic and hardware
 * logic and provides a standard structure for every output.
 *
 * Design note: channels are 0-indexed. The mapping from logical channel
 * number to physical pin/register is internal to the driver. For
 * example, an output_relay driver might map:
 *   channel 0 -> MCP23017 GPA0 -> ULN2803A IN1 -> Relay 1
 *   channel 1 -> MCP23017 GPA1 -> ULN2803A IN2 -> Relay 2
 */

#pragma once

#include "bus/i2c_bus.h"
#include "esp_err.h"
#include "stdbool.h"
#include "stdint.h"

#ifdef __cplusplus
extern "C" {
#endif

/* --------------------------- CONFIG STRUCT ----------------------------*/
/**
 * @brief Configuration structure for initializing an output driver instance.
 *
 * @param bus           Pointer to the shared I2C bus instance (NULL if using native GPIOs)
 * @param i2c_addr      7-bit I2C address of the target expander device (e.g. 0x20 for MCP23017)
 * @param num_channels  Total number of output pins or channels the driver is expected to manage
 * @param extra         Generic pointer to a custom, driver-specific configuration
 */
typedef struct {
    i2c_bus_t *bus;
    uint8_t    i2c_addr;
    uint8_t    num_channels;
    void      *extra;
} output_config_t;

/**
 * @brief Output driver interface (vtable).
 * Defines the unified contract for all multi-channel output operations.
 * Single access path: callers only ever reach a concrete output
 * implementation through these function pointers.
 *
 * num_channels here is a property of the driver type itself (how many
 * channels this concrete driver manages), separate from the num_channels
 * passed in output_config_t at init time.
 *
 * @param cfg      Pointer to the output_config_t structure
 * @param channel  Zero-indexed hardware channel or pin number
 * @param state    true drives the channel high/active, false drives it low/inactive
 * @param bitmask  8-bit map where bit 0 corresponds to channel 0
 *
 * @return
 * - ESP_OK   : Operation completed successfully
 * - ESP_FAIL / Specific esp_err_t : Hardware communication error or invalid channel index
 * - void     : No return value
 */
typedef struct {
    const char *name;
    uint8_t     num_channels;

    /**
     * @brief Initialize hardware, configure pin directions, default all channels OFF
     */
    esp_err_t (*init)(const output_config_t *cfg);
    /**
     * @brief Set a single channel state to ON or OFF
     */
    esp_err_t (*set)(uint8_t channel, bool state);
    /**
     * @brief Read back the current latch or register state of a single channel
     */
    esp_err_t (*get)(uint8_t channel, bool *state);
    /**
     * @brief Update all channels simultaneously using an 8-bit state map
     */
    esp_err_t (*set_all)(uint8_t bitmask);
    /**
     * @brief Release hardware resources and reset expander state (nullable)
     */
    void (*deinit)(void);
} output_driver_t;

#ifdef __cplusplus
}
#endif