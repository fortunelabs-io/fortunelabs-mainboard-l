// SPDX-License-Identifier: Apache-2.0
/**
 * @file actuator_dummy.c
 * @brief Native GPIO implementation for ESP32 Onboard LED matching the HAL contract.
 */

#include "driver/gpio.h"
#include "esp_log.h"
#include "hal/output_driver.h"

#define DUMMY_LED_GPIO 2
#define DUMMY_NUM_CHANNELS 1

static const char *TAG = "ACTUATOR_DUMMY";

static bool s_is_initialized = false;
static bool s_channel_state  = false;

/**
 * @brief Initialize native GPIO for the onboard LED
 */
static esp_err_t dummy_output_init(const output_config_t *cfg) {
    if (s_is_initialized) {
        ESP_LOGW(TAG, "Actuator driver already initialized.");
        return ESP_OK;
    }

    gpio_config_t io_conf = {.pin_bit_mask = (1ULL << DUMMY_LED_GPIO),
                             .mode         = GPIO_MODE_OUTPUT,
                             .pull_up_en   = GPIO_PULLUP_DISABLE,
                             .pull_down_en = GPIO_PULLDOWN_DISABLE,
                             .intr_type    = GPIO_INTR_DISABLE};

    esp_err_t err = gpio_config(&io_conf);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure GPIO %d", DUMMY_LED_GPIO);
        return err;
    }

    gpio_set_level(DUMMY_LED_GPIO, 0);
    s_channel_state  = false;
    s_is_initialized = true;

    ESP_LOGI(TAG, "Dummy Actuator initialized on GPIO %d.", DUMMY_LED_GPIO);
    return ESP_OK;
}

/**
 * @brief Set single channel state (Channel 0 = LED)
 */
static esp_err_t dummy_output_set(uint8_t channel, bool state) {
    if (!s_is_initialized)
        return ESP_FAIL;

    if (channel >= DUMMY_NUM_CHANNELS) {
        ESP_LOGE(TAG, "Invalid channel index: %d", channel);
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t err = gpio_set_level(DUMMY_LED_GPIO, state ? 1 : 0);
    if (err == ESP_OK) {
        s_channel_state = state;
        ESP_LOGD(TAG, "Channel %d set to %s", channel, state ? "ON" : "OFF");
    }

    return err;
}

/**
 * @brief Read back the current latch state of the channel
 */
static esp_err_t dummy_output_get(uint8_t channel, bool *state) {
    if (!s_is_initialized || state == NULL)
        return ESP_FAIL;

    if (channel >= DUMMY_NUM_CHANNELS) {
        return ESP_ERR_INVALID_ARG;
    }

    *state = s_channel_state;
    return ESP_OK;
}

/**
 * @brief Update all channels simultaneously using an 8-bit map
 * Bit 0 corresponds to Channel 0 (LED)
 */
static esp_err_t dummy_output_set_all(uint8_t bitmask) {
    if (!s_is_initialized)
        return ESP_FAIL;

    bool led_target_state = (bitmask & (1 << 0)) ? true : false;

    return dummy_output_set(0, led_target_state);
}

/**
 * @brief Release hardware resources
 */
static void dummy_output_deinit(void) {
    if (!s_is_initialized)
        return;

    gpio_reset_pin(DUMMY_LED_GPIO);
    s_is_initialized = false;
    s_channel_state  = false;

    ESP_LOGI(TAG, "Dummy Actuator deinitialized.");
}

/* --- Vtable Registration --- */
const output_driver_t actuator_dummy_driver = {.name         = "LED_ONBOARD_DUMMY",
                                               .num_channels = DUMMY_NUM_CHANNELS,
                                               .init         = dummy_output_init,
                                               .set          = dummy_output_set,
                                               .get          = dummy_output_get,
                                               .set_all      = dummy_output_set_all,
                                               .deinit       = dummy_output_deinit};