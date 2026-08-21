// SPDX-License-Identifier: Apache-2.0

/**
 * @file status_led.h
 * @brief Native GPIO status LED implementing the output HAL contract.
 *
 * Drives the MCU status LED, the only indicator reachable without the I2C
 * bus. On the v0 mainboard the D901-D908 bank hangs off the ULN2803A behind
 * the MCP23017, so it cannot report a fault in the I2C path itself; this one
 * can. Today that LED is an external one on the development board.
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
 * GPIO 4 targets the ESP32 devkit this firmware is currently developed on,
 * with an external LED and series resistor. It is not a strapping pin (0, 2,
 * 5, 12, 15 are) and does not emit a PWM burst during boot, so the LED is
 * dark until this driver drives it - which is what makes the heartbeat
 * starting mean something.
 *
 * GPIO 2 is deliberately not used: actuator_dummy already owns it as the
 * devkit's onboard LED, and two drivers configuring one pad would fight.
 *
 * On the v0 mainboard this moves to whichever pad D201 lands on. That is a
 * one-line change in main.c, which is why the pin arrives through
 * output_config_t::extra instead of being fixed here.
 */
#define STATUS_LED_DEFAULT_GPIO GPIO_NUM_4

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
