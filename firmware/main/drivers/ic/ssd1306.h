// SPDX-License-Identifier: MIT
// Derived from https://github.com/nopnop2002/esp-idf-ssd1306
/**
 * @file ssd1306.h
 * @brief Real SSD1306 OLED Driver Interface via Custom I2C Bus wrapper.
 */

#pragma once

#include "hal/display_driver.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Exported vTable instance for the physical SSD1306 OLED display.
 */
extern const display_driver_t ssd1306_driver;

#ifdef __cplusplus
}
#endif