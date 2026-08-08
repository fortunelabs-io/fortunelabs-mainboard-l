// SPDX-License-Identifier: Apache-2.0
/**
 * @brief Implementation of simulation logic for a sweeping potentiometer.
 */
#include "esp_log.h"
#include "esp_timer.h"
#include "hal/sensor_driver.h"

#define SIM_VOLTAGE_MIN 0.0f
#define SIM_VOLTAGE_MAX 3.3f
#define SIM_STEP_SIZE 0.1f

static const char *TAG = "sensor_dummy";

static float s_current_voltage = 0.0f;
static bool  s_direction_up    = true;
static bool  s_is_initialized  = false;

/**
 * @brief Init dummy sensor state
 */
static esp_err_t dummy_init(const sensor_config_t *cfg) {
    if (s_is_initialized) {
        ESP_LOGW(TAG, "sensor already initialized");
        return ESP_OK;
    }

    s_current_voltage = SIM_VOLTAGE_MIN;
    s_direction_up    = true;
    s_is_initialized  = true;

    ESP_LOGI(TAG, "dummy potentiometer driver initialized successfully");
    return ESP_OK;
}

/**
 * @brief Simulate potentiometer turning up and down (Triangle Wave Logic)
 */
static esp_err_t dummy_read(sensor_reading_t *out) {
    if (!s_is_initialized || out == NULL) {
        return ESP_FAIL;
    }

    // Potentio sweep logic
    if (s_direction_up) {
        s_current_voltage += SIM_STEP_SIZE;
        if (s_current_voltage >= SIM_VOLTAGE_MAX) {
            s_current_voltage = SIM_VOLTAGE_MAX;
            s_direction_up    = false; // decrease
        }
    } else {
        s_current_voltage -= SIM_STEP_SIZE;
        if (s_current_voltage <= SIM_VOLTAGE_MIN) {
            s_current_voltage = SIM_VOLTAGE_MIN;
            s_direction_up    = true; // increase
        }
    }

    // Put data to struct output as is HAL contract
    out->channel      = 0; // Default channel
    out->value        = s_current_voltage;
    out->timestamp_ms = (uint32_t)(esp_timer_get_time() / 1000); // Put uptime in miliseconds

    return ESP_OK;
}

/**
 * @brief Deinitialize dummy sensor
 */
static void dummy_deinit(void) {
    s_is_initialized = false;
    ESP_LOGI(TAG, "Dummy Potentiometer driver deinitialized.");
}

// Vtable regis
const sensor_driver_t sensor_dummy_driver = {
    .name = "POTENTIO_DUMMY", .init = dummy_init, .read = dummy_read, .deinit = dummy_deinit};