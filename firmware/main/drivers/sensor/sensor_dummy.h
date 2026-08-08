// SPDX-License-Identifier: Apache-2.0
/**
 * @file sensor_dummy.h
 * @brief Dummy Potentiometer Sensor Driver Interface.
 */

#pragma once

#include "hal/sensor_driver.h"

#ifdef __cplusplus
extern "C" {
#endif
/**
 *@brief Exported instance of the dummy sensor driver.
 */
extern const sensor_driver_t sensor_dummy_driver;

#ifdef __cplusplus
}
#endif