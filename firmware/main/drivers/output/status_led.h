// SPDX-License-Identifier: Apache-2.0

/**
 * @file status_led.h
 * @brief Native GPIO status LED implementing the output HAL contract.
 *
 * Drives the board's MCU status LED (D201), the only indicator reachable
 * without the I2C bus. D901-D908 hang off the ULN2803A behind the MCP23017,
 * so they cannot report a fault in the I2C path itself; this one can.
 *
 * Single channel: channel 0 maps to the configured GPIO. The pin is supplied
 * through output_config_t::extra rather than fixed here, so a board revision
 * that moves the LED is a wiring change in main.c, not a driver edit.
 */

#pragma once

#include "driver/gpio.h"
#include "hal/output_driver.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Pin used when no status_led_config_t is supplied.
 *
 * IO48 carries no strapping function on the ESP32-S3 and is Espressif's own
 * convention for the module status LED, so a probe on this pin means the same
 * thing here as it does on a devkit.
 */
#define STATUS_LED_DEFAULT_GPIO GPIO_NUM_48

/**
 * @brief Driver-specific configuration, passed via output_config_t::extra.
 *
 * @param gpio        GPIO the LED anode (or cathode, when active_low) sits on.
 * @param active_low  true when the GPIO sinks current through the LED, so a
 *                    logic low lights it. false for a source-driven LED.
 */
typedef struct {
    gpio_num_t gpio;
    bool       active_low;
} status_led_config_t;

/**
 * @brief Output contract for the status LED. Wire this into a task context.
 */
extern const output_driver_t status_led_driver;

#ifdef __cplusplus
}
#endif
