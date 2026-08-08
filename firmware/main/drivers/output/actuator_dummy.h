// SPDX-License-Identifier: Apache-2.0
/**
 * @file actuator_dummy.h
 * @brief Dummy Actuator (Onboard LED) Driver Interface.
 */

#pragma once

#include "hal/output_driver.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Exported instance of the dummy actuator driver.
 * Controls the onboard LED via Channel 0.
 */
extern const output_driver_t actuator_dummy_driver;

#ifdef __cplusplus
}
#endif