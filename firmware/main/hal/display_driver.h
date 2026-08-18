// SPDX-License-Identifier: Apache-2.0

/**
 * @file display_driver.h
 * @brief Display HAL Contract
 *
 * Any display interface implements this contract.
 *
 * Design note: display_show_text() uses row-based addressing (like a
 * terminal), not pixel coordinates. This keeps the interface simple and
 * works across very different displays. For pixel-level control, use
 * display_raw() (future extension).
 */

#pragma once

#include "bus/i2c_bus.h"
#include "esp_err.h"
#include "stdint.h"

#ifdef __cplusplus
extern "C" {
#endif

/* --------------------------- CAPACITY ----------------------------*/
/**
 * @brief Maximum characters one row of text may carry through this contract,
 *        excluding the null terminator.
 *
 * A capability of the interface, not a property of any panel. Callers may
 * send up to this many characters; an implementation renders as many as its
 * own geometry allows and truncates the rest. A concrete driver derives its
 * real width from display_config_t::width, never from this value, so a
 * narrower or wider display needs no change here.
 */
#define DISPLAY_TEXT_MAX_LEN 32

/* --------------------------- CONFIG STRUCT ----------------------------*/
/**
 * @brief Configuration structure for initializing a display driver instance.
 *
 * @param bus       Pointer to the initialized I2C bus instance
 * @param i2c_addr  7-bit I2C address of the target display device
 * @param width     Physical horizontal resolution or character capacity
 * @param height    Physical vertical resolution or row capacity
 * @param extra     Generic pointer to a custom, driver-specific configuration
 */
typedef struct {
    i2c_bus_t *bus;
    uint8_t    i2c_addr;
    uint16_t   width;
    uint16_t   height;
    void      *extra;
} display_config_t;

/**
 * @brief Display driver interface (vtable).
 * Defines the unified contract for all display operations. Single access
 * path: callers only ever reach a concrete display implementation through
 * these function pointers.
 *
 * @param cfg    Pointer to the display_config_t structure
 * @param row    Zero-indexed target row number for text placement
 * @param text   Null-terminated string to display
 * @param level  Brightness intensity, 0 to 255
 *
 * @return
 * - ESP_OK   : Operation completed successfully
 * - ESP_FAIL / Specific esp_err_t : Hardware communication or configuration error
 * - void     : No return value
 */
typedef struct {
    const char *name;

    /**
     * @brief Initialize display hardware
     */
    esp_err_t (*init)(const display_config_t *cfg);
    /**
     * @brief Clear the entire display
     */
    esp_err_t (*clear)(void);
    /**
     * @brief Show a line of text at the given row
     */
    esp_err_t (*show_text)(uint8_t row, const char *text);
    /**
     * @brief Set display brightness (0-255)
     */
    esp_err_t (*set_brightness)(uint8_t level);
    /**
     * @brief Release display resources (nullable)
     */
    void (*deinit)(void);
} display_driver_t;

#ifdef __cplusplus
}
#endif