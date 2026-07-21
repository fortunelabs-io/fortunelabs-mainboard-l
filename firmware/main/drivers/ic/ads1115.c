/**
 * @file ads1115.c
 * @brief ADS1115 16-bit I2C ADC Driver Implementation
 */

#include "drivers/ic/ads1115.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "ads1115_driver";

/* ---------------------------- LOOKUP TABLES -------------------------- */
/**
 * @brief Conversion period lookup, indexed by ads1115_data_rate_t.
 *
 * Values are the datasheet single-shot conversion time in milliseconds,
 * rounded up. ADS1115_CONVERSION_MARGIN_MS is added on top before use.
 */
static const uint32_t s_conversion_to_ms[] = {
    [ADS1115_DR_16SPS] = 63, [ADS1115_DR_32SPS] = 32, [ADS1115_DR_64SPS] = 16,
    [ADS1115_DR_128SPS] = 8, [ADS1115_DR_250SPS] = 4, [ADS1115_DR_475SPS] = 3,
    [ADS1115_DR_860SPS] = 2,
};

/* ---------------------------- PRIVATE HELPER -------------------------- */
/**
 * @brief Guard: validates that a channel enum value is within range.
 *
 * @param channel  Channel value to validate
 *
 * @return
 * - true  : channel is one of ADS1115_CHANNEL_0..3
 * - false : channel is out of range
 */
static inline bool _ads1115_channel_valid(ads1115_channel_t channel) {
    return (channel >= ADS1115_CHANNEL_0) && (channel <= ADS1115_CHANNEL_3);
}

/**
 * @brief Build the 16-bit config register word for a single-shot conversion.
 *
 * Packs OS, MUX, PGA, MODE, DR, and COMP_QUE fields per the ADS1115
 * config register layout. COMP_QUE is always set to disabled since the
 * comparator/ALERT function is not used by this driver.
 *
 * @param config  Pointer to the per-channel config supplying MUX/PGA/DR fields
 *
 * @return 16-bit config register value ready to write to ADS1115_REG_CONFIG
 */
static uint16_t _ads1115_build_config_word(const ads1115_channel_config_t *config) {
    uint16_t word = 0;
    word |= (1U << 15);
    word |= ((uint16_t)config->channel & 0x07) << 12; // MUX
    word |= ((uint16_t)config->pga & 0x07) << 9;      // PGA
    word |= (1U << 8);                                // MODE
    word |= ((uint8_t)config->data_rate & 0x07) << 5; // DR
    word |= 0x03;                                     // COMP_QUE = Disabled

    return word;
}

/**
 * @brief Write a 16-bit value to a single internal ADS1115 register over I2C.
 *
 * Constructs a 3-byte payload [reg, MSB, LSB] passed to i2c_bus_write().
 * The ADS1115 expects big-endian register values.
 *
 * @param dev    Pointer to ads1115_dev_t context struct
 * @param reg    Target register address
 * @param value  16-bit value to write, MSB first
 *
 * @return
 * - ESP_OK   : Register written successfully
 * - ESP_FAIL : I2C transaction error (no ACK or bus fault)
 */
static esp_err_t _ads1115_write_register(ads1115_dev_t *dev, uint8_t reg, uint16_t value) {
    uint8_t payload[3] = {
        reg,
        (uint8_t)((value >> 8) & 0xFF),
        (uint8_t)(value & 0xFF),
    };
    return i2c_bus_write(dev->bus, dev->dev, payload, sizeof(payload));
}

/**
 * @brief Read a 16-bit value from a single internal ADS1115 register over I2C.
 *
 * Issues a write (sets the internal register pointer) then a repeated-start
 * read of 2 bytes, reassembled big-endian into a 16-bit value.
 *
 * @param dev        Pointer to ads1115_dev_t context struct
 * @param reg        Target register address
 * @param out_value  Pointer to uint16_t that receives the register value
 *
 * @return
 * - ESP_OK   : Register read successfully, out_value populated
 * - ESP_FAIL : I2C transaction error
 */
static esp_err_t _ads1115_read_register(ads1115_dev_t *dev, uint8_t reg, uint16_t *out_value) {
    uint8_t read_buf[2] = {0};

    esp_err_t ret = i2c_bus_write_read(dev->bus, dev->dev, &reg, 1, read_buf, sizeof(read_buf));
    if (ret != ESP_OK)
        return ret;

    *out_value = ((uint16_t)read_buf[0] << 8) | read_buf[1];
    return ESP_OK;
}

/* ---------------------------- vTable Implementation -------------------------- */
// 1. Init
static esp_err_t ads1115_init(ads1115_dev_t *dev, const ads1115_config_t *config) {
    if (dev == NULL || config == NULL || config->bus == NULL)
        return ESP_ERR_INVALID_ARG;

    memset(dev, 0, sizeof(ads1115_dev_t));

    esp_err_t ret =
        i2c_bus_add_device(config->bus, (uint8_t)config->addr, config->scl_hz, "ADS1115", &dev->dev);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register device 0x%02X: %s", config->addr, esp_err_to_name(ret));
        return ret;
    }

    dev->bus = config->bus;
    memcpy(dev->channel_config, config->channel_config, sizeof(dev->channel_config));

    // Idempotent safety: restore chip to known state on every init
    ret = _ads1115_write_register(dev, ADS1115_REG_CONFIG, ADS1115_CONFIG_RESET);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to write reset config: %s", esp_err_to_name(ret));
        return ret;
    }

    dev->initialized = true;
    ESP_LOGI(TAG, "Init OK, addr=0x%02X", config->addr);

    return ESP_OK;
}

// 2. Read
static esp_err_t ads1115_read(ads1115_dev_t *dev, ads1115_channel_t channel, uint16_t *out_raw) {
    if (dev == NULL || out_raw == NULL)
        return ESP_ERR_INVALID_ARG;
    if (!dev->initialized)
        return ESP_ERR_INVALID_STATE;
    if (!_ads1115_channel_valid(channel))
        return ESP_ERR_INVALID_ARG;

    // Map channel enum to channel_config array index
    const ads1115_channel_config_t *channel_config =
        &dev->channel_config[channel - ADS1115_CHANNEL_0];

    uint16_t config_word = _ads1115_build_config_word(channel_config);

    esp_err_t ret = _ads1115_write_register(dev, ADS1115_REG_CONFIG, config_word);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Config write failed %s", esp_err_to_name(ret));
        return ret;
    }

    uint32_t delay_ms =
        s_conversion_to_ms[channel_config->data_rate] + ADS1115_CONVERSION_MARGIN_MS;
    vTaskDelay(pdMS_TO_TICKS(delay_ms));

    uint16_t raw_value = 0;
    ret                = _ads1115_read_register(dev, ADS1115_REG_CONVERSION, &raw_value);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Conversion read failed: %s", esp_err_to_name(ret));
        return ret;
    }

    *out_raw = (uint16_t)raw_value;
    return ESP_OK;
}

// 3. Reset
static esp_err_t ads1115_reset(ads1115_dev_t *dev) {
    if (dev == NULL)
        return ESP_ERR_INVALID_ARG;
    if (!dev->initialized)
        return ESP_ERR_INVALID_STATE;

    esp_err_t ret = _ads1115_write_register(dev, ADS1115_REG_CONFIG, ADS1115_CONFIG_RESET);
    if (ret != ESP_OK)
        ESP_LOGW(TAG, "Reset failed: %s", esp_err_to_name(ret));

    return ret;
}

// 4. De-Initialization
static void ads1115_deinit(ads1115_dev_t *dev) {
    if (dev == NULL)
        return;

    dev->initialized = false;
    ESP_LOGI(TAG, "Driver de-initialized");
}

// * vTable Singleton
static const ads1115_driver_t s_ads1115_driver = {
    .init   = ads1115_init,
    .read   = ads1115_read,
    .reset  = ads1115_reset,
    .deinit = ads1115_deinit,
};

const ads1115_driver_t *ads1115_get_driver(void) { return &s_ads1115_driver; }