// SPDX-License-Identifier: Apache-2.0

/**
 * @file status_led.c
 * @brief Native GPIO implementation of the output HAL contract for the
 *        MCU status LED (D201).
 */

#include "drivers/output/status_led.h"
#include "esp_log.h"
#include "hal/output_driver.h"

#define STATUS_LED_NUM_CHANNELS 1

static const char *TAG = "status_led";

static bool       s_is_initialized = false;
static bool       s_channel_state  = false;
static gpio_num_t s_gpio           = STATUS_LED_DEFAULT_GPIO;
static bool       s_active_low     = false;

/**
 * @brief Translate a logical channel state into the level the pin must carry.
 *
 * Keeps the active_low inversion in exactly one place, so the vtable
 * functions below never reason about polarity.
 *
 * @param state  true for lit, false for dark.
 * @return 1 or 0, the level to write to the GPIO.
 */
static uint32_t _status_led_level(bool state) {
    return (state != s_active_low) ? 1U : 0U;
}

/**
 * @brief Configure the status LED GPIO as a push-pull output, LED dark.
 *
 * Reads the pin and polarity from cfg->extra when supplied, otherwise falls
 * back to STATUS_LED_DEFAULT_GPIO driven active high.
 *
 * @param cfg  Output configuration. May be NULL, and cfg->extra may be NULL;
 *             both select the defaults. cfg->bus is unused: this driver is
 *             native GPIO and never touches I2C.
 * @return
 * - ESP_OK              : GPIO configured and driven to the dark state
 * - ESP_OK              : driver was already initialized (logged as a warning)
 * - ESP_ERR_INVALID_ARG : the configured pin is not a valid output GPIO
 * - other esp_err_t     : as returned by gpio_config()
 */
static esp_err_t status_led_init(const output_config_t *cfg) {
    if (s_is_initialized) {
        ESP_LOGW(TAG, "Status LED driver already initialized.");
        return ESP_OK;
    }

    gpio_num_t gpio       = STATUS_LED_DEFAULT_GPIO;
    bool       active_low = false;

    if (cfg != NULL && cfg->extra != NULL) {
        const status_led_config_t *led_cfg = (const status_led_config_t *)cfg->extra;
        gpio                               = led_cfg->gpio;
        active_low                         = led_cfg->active_low;
    }

    if (!GPIO_IS_VALID_OUTPUT_GPIO(gpio)) {
        ESP_LOGE(TAG, "GPIO %d cannot drive an output.", (int)gpio);
        return ESP_ERR_INVALID_ARG;
    }

    gpio_config_t io_conf = {.pin_bit_mask = (1ULL << gpio),
                             .mode         = GPIO_MODE_OUTPUT,
                             .pull_up_en   = GPIO_PULLUP_DISABLE,
                             .pull_down_en = GPIO_PULLDOWN_DISABLE,
                             .intr_type    = GPIO_INTR_DISABLE};

    esp_err_t err = gpio_config(&io_conf);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure GPIO %d: %s", (int)gpio, esp_err_to_name(err));
        return err;
    }

    s_gpio           = gpio;
    s_active_low     = active_low;
    s_channel_state  = false;
    s_is_initialized = true;

    gpio_set_level(s_gpio, _status_led_level(false));

    ESP_LOGI(TAG, "Status LED initialized on GPIO %d (active %s).", (int)s_gpio,
             s_active_low ? "low" : "high");
    return ESP_OK;
}

/**
 * @brief Light or extinguish the status LED.
 *
 * @param channel  Must be 0; this driver manages a single LED.
 * @param state    true lights the LED, false extinguishes it.
 * @return
 * - ESP_OK              : level written and cached
 * - ESP_FAIL            : driver not initialized
 * - ESP_ERR_INVALID_ARG : channel is out of range
 * - other esp_err_t     : as returned by gpio_set_level()
 */
static esp_err_t status_led_set(uint8_t channel, bool state) {
    if (!s_is_initialized)
        return ESP_FAIL;

    if (channel >= STATUS_LED_NUM_CHANNELS) {
        ESP_LOGE(TAG, "Invalid channel index: %d", channel);
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t err = gpio_set_level(s_gpio, _status_led_level(state));
    if (err == ESP_OK) {
        s_channel_state = state;
    }

    return err;
}

/**
 * @brief Read back the cached state of the status LED.
 *
 * Returns the last state this driver wrote rather than sampling the pad: the
 * pin is configured output-only, so a read back would not be meaningful.
 *
 * @param channel  Must be 0; this driver manages a single LED.
 * @param state    Receives true when lit, false when dark. Must not be NULL.
 * @return
 * - ESP_OK              : *state written
 * - ESP_FAIL            : driver not initialized, or state is NULL
 * - ESP_ERR_INVALID_ARG : channel is out of range
 */
static esp_err_t status_led_get(uint8_t channel, bool *state) {
    if (!s_is_initialized || state == NULL)
        return ESP_FAIL;

    if (channel >= STATUS_LED_NUM_CHANNELS) {
        return ESP_ERR_INVALID_ARG;
    }

    *state = s_channel_state;
    return ESP_OK;
}

/**
 * @brief Drive the LED from an 8-bit map. Bit 0 is the LED; higher bits are
 *        ignored, as this driver owns one channel.
 *
 * @param bitmask  State map; bit 0 set lights the LED.
 * @return
 * - ESP_OK          : level written
 * - ESP_FAIL        : driver not initialized
 * - other esp_err_t : as returned by status_led_set()
 */
static esp_err_t status_led_set_all(uint8_t bitmask) {
    if (!s_is_initialized)
        return ESP_FAIL;

    return status_led_set(0, (bitmask & (1U << 0)) != 0);
}

/**
 * @brief Extinguish the LED and return the GPIO to its reset state.
 *
 * @return void
 */
static void status_led_deinit(void) {
    if (!s_is_initialized)
        return;

    gpio_set_level(s_gpio, _status_led_level(false));
    gpio_reset_pin(s_gpio);

    s_is_initialized = false;
    s_channel_state  = false;

    ESP_LOGI(TAG, "Status LED deinitialized.");
}

/* --- Vtable Registration --- */
const output_driver_t status_led_driver = {.name         = "LED_STATUS_D201",
                                           .num_channels = STATUS_LED_NUM_CHANNELS,
                                           .init         = status_led_init,
                                           .set          = status_led_set,
                                           .get          = status_led_get,
                                           .set_all      = status_led_set_all,
                                           .deinit       = status_led_deinit};
