// SPDX-License-Identifier: Apache-2.0
/**
 * @file sensor_ads1115.c
 * @brief ADS1115 Sensor HAL Adapter Implementation
 */

#include "drivers/sensor/sensor_ads1115.h"
#include "esp_log.h"
#include "esp_timer.h"

static const char *TAG = "sensor_ads1115";

/* Per-device I2C clock for the ADS1115. Supplied by this adapter (the
 * config layer) rather than hardcoded inside the driver, per Decision #1. */
#define SENSOR_ADS1115_SCL_HZ 100000

/* Backing state for the singleton contract. One active device, one
 * active channel, resolved at init. */
static const ads1115_driver_t *s_drv               = NULL;
static ads1115_dev_t           s_dev               = {0};
static ads1115_channel_t       s_active_channel    = ADS1115_CHANNEL_0;
static uint8_t                 s_active_channel_id = 0;
static float                   s_active_lsb_volts  = 0.0f;
static bool                    s_initialized       = false;

/* ---------------------------- PRIVATE HELPER -------------------------- */
/**
 * @brief Volts represented by one LSB for a given PGA full-scale range.
 *
 * The ADS1115 is a signed 16-bit converter, so full scale spans 32768
 * codes on each side. LSB volts is the full-scale range divided by
 * 32768.
 *
 * @param pga  Programmable gain setting for the active channel
 *
 * @return Volts per code. Falls back to the 4.096V range if the value
 *         is unrecognized, matching the driver's mid-range default.
 */
static float _ads1115_lsb_volts(ads1115_pga_t pga) {
    switch (pga) {
        case ADS1115_PGA_6_144V:
            return 6.144f / 32768.0f;
        case ADS1115_PGA_4_096V:
            return 4.096f / 32768.0f;
        case ADS1115_PGA_2_048V:
            return 2.048f / 32768.0f;
        case ADS1115_PGA_1_024V:
            return 1.024f / 32768.0f;
        case ADS1115_PGA_0_512V:
            return 0.512f / 32768.0f;
        case ADS1115_PGA_0_256V:
            return 0.256f / 32768.0f;
        default:
            return 4.096f / 32768.0f;
    }
}

/* --------------------------- CONTRACT METHODS ----------------------------*/
/**
 * @brief Initialize the underlying ADS1115 and resolve the active channel.
 *
 * @param cfg  Generic sensor config. bus, i2c_addr, and channel are read
 *             directly; extra must point to a sensor_ads1115_config_t.
 *
 * @return
 * - ESP_OK              : Device initialized, active channel resolved
 * - ESP_ERR_INVALID_ARG : cfg, bus, or extra is NULL, or channel > 3
 * - Specific esp_err_t  : Underlying driver init failure
 */
static esp_err_t ads1115_sensor_init(const sensor_config_t *cfg) {
    if (s_initialized) {
        ESP_LOGW(TAG, "sensor already initialized");
        return ESP_OK;
    }
    if (cfg == NULL || cfg->bus == NULL || cfg->extra == NULL || cfg->channel > 3) {
        return ESP_ERR_INVALID_ARG;
    }

    const sensor_ads1115_config_t *ext = (const sensor_ads1115_config_t *)cfg->extra;
    s_drv                              = ads1115_get_driver();

    ads1115_config_t ads_cfg = {
        .bus    = cfg->bus,
        .addr   = (ads1115_addr_t)cfg->i2c_addr,
        .scl_hz = SENSOR_ADS1115_SCL_HZ,
    };
    for (int i = 0; i < 4; i++) {
        ads_cfg.channel_config[i] = ext->channel_config[i];
    }

    esp_err_t err = s_drv->init(&s_dev, &ads_cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "underlying ads1115 init failed: %d", err);
        return err;
    }

    s_active_channel_id = cfg->channel;
    s_active_channel    = (ads1115_channel_t)(ADS1115_CHANNEL_0 + cfg->channel);
    s_active_lsb_volts  = _ads1115_lsb_volts(ext->channel_config[cfg->channel].pga);
    s_initialized       = true;

    ESP_LOGI(TAG, "ads1115 sensor adapter ready on channel %u", s_active_channel_id);
    return ESP_OK;
}

/**
 * @brief Trigger one conversion on the active channel and return volts.
 *
 * The raw register value is interpreted as a signed 16-bit code before
 * scaling, so readings near and below zero are represented correctly
 * rather than wrapping to a large positive voltage.
 *
 * @param out  Destination sample. channel, value (volts), and
 *             timestamp_ms are populated on success.
 *
 * @return
 * - ESP_OK                : Sample stored in out
 * - ESP_ERR_INVALID_STATE : init() has not completed
 * - ESP_ERR_INVALID_ARG   : out is NULL
 * - Specific esp_err_t     : Underlying conversion or I2C failure
 */
static esp_err_t ads1115_sensor_read(sensor_reading_t *out) {
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    if (out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    uint16_t  raw = 0;
    esp_err_t err = s_drv->read(&s_dev, s_active_channel, &raw);
    if (err != ESP_OK) {
        return err;
    }

    int16_t signed_code = (int16_t)raw;

    out->channel      = s_active_channel_id;
    out->value        = (float)signed_code * s_active_lsb_volts;
    out->timestamp_ms = (uint32_t)(esp_timer_get_time() / 1000);
    return ESP_OK;
}

/**
 * @brief Release the underlying ADS1115 and clear adapter state.
 *
 * @return void
 */
static void ads1115_sensor_deinit(void) {
    if (s_drv != NULL && s_initialized) {
        s_drv->deinit(&s_dev);
    }
    s_initialized = false;
    ESP_LOGI(TAG, "ads1115 sensor adapter deinitialized");
}

/* --------------------------- VTABLE REGISTRATION ----------------------------*/
const sensor_driver_t sensor_ads1115_driver = {
    .name   = "ADS1115",
    .init   = ads1115_sensor_init,
    .read   = ads1115_sensor_read,
    .deinit = ads1115_sensor_deinit,
};