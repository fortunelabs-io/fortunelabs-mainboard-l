/**
 * @file ads1115.h
 * @brief ADS1115 16-bit I2C ADC Driver
 *
 * Driver for the Texas Instruments ADS1115 16-bit delta-sigma ADC.
 * Communicates over I2C using the platform i2c_bus abstraction layer.
 * Operates in single-shot conversion mode; each read() triggers one
 * conversion, blocks for the data-rate-derived conversion period, then
 * retrieves the raw result.
 *
 * Per-channel configuration (PGA, data rate) is stored at init time
 * rather than as a single global config, so each of the 4 input
 * channels can be tuned independently for mainboard flexibility.
 *
 * Access pattern: vtable (ads1115_driver_t) obtained via
 * ads1115_get_driver(). All operations are reached through the
 * function pointers in this vtable; there are no other public entry
 * points into the driver.
 *
 * @note All vtable methods except init() return ESP_ERR_INVALID_STATE if
 *       called before init() completes successfully.
 */

#pragma once

#include "bus/i2c_bus.h"
#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------ REGISTER MAP ---------------------------------*/
#define ADS1115_REG_CONVERSION 0x00   ///< Conversion result register (read-only)
#define ADS1115_REG_CONFIG 0x01       ///< Configuration register
#define ADS1115_REG_LO_THRESHOLD 0x02 ///< Low threshold register (comparator, unused)
#define ADS1115_REG_HI_THRESHOLD 0x03 ///< High threshold register (comparator, unused)

/* -------------------------- CONSTANT VARIABLE --------------------------*/
#define ADS1115_CONFIG_RESET 0x8583U    ///< Factory default config register value
#define ADS1115_CONVERSION_MARGIN_MS 2U ///< Extra delay added on top of data-rate conversion time

/* --------------------------- ENUMERATION ----------------------------*/
/**
 * @brief I2C address selection, set by the ADDR pin strap.
 */
typedef enum {
    ADS1115_ADDR_GND = 0x48, ///< ADDR tied to GND
    ADS1115_ADDR_VDD = 0x49, ///< ADDR tied to VDD
    ADS1115_ADDR_SDA = 0x4A, ///< ADDR tied to SDA
    ADS1115_ADDR_SCL = 0x4B, ///< ADDR tied to SCL
} ads1115_addr_t;

/**
 * @brief Input MUX channel selection (single-ended, AINx vs GND).
 */
typedef enum {
    ADS1115_CHANNEL_0 = 4, // 0b100
    ADS1115_CHANNEL_1 = 5, // 0b101
    ADS1115_CHANNEL_2 = 6, // 0b110
    ADS1115_CHANNEL_3 = 7, // 0b111
} ads1115_channel_t;

/**
 * @brief Programmable Gain Amplifier (PGA) full-scale range selection.
 */
typedef enum {
    ADS1115_PGA_6_144V = 0, // 0b000 -> ±6.144V FSR
    ADS1115_PGA_4_096V = 1, // 0b001 -> ±4.096V FSR
    ADS1115_PGA_2_048V = 2, // 0b010 -> ±2.048V FSR
    ADS1115_PGA_1_024V = 3, // 0b011 -> ±1.024V FSR
    ADS1115_PGA_0_512V = 4, // 0b100 -> ±0.512V FSR
    ADS1115_PGA_0_256V = 5, // 0b101 -> ±0.256V FSR
} ads1115_pga_t;

/**
 * @brief Output data rate selection, determines single-shot conversion time.
 */
typedef enum {
    ADS1115_DR_16SPS  = 1, // 0b001 -> 16SPS,  ~62.5ms conversion
    ADS1115_DR_32SPS  = 2, // 0b010 -> 32SPS,  ~31.25ms conversion
    ADS1115_DR_64SPS  = 3, // 0b011 -> 64SPS,  ~15.6ms conversion
    ADS1115_DR_128SPS = 4, // 0b100 -> 128SPS, ~7.8ms conversion
    ADS1115_DR_250SPS = 5, // 0b101 -> 250SPS, ~4.0ms conversion
    ADS1115_DR_475SPS = 6, // 0b110 -> 475SPS, ~2.1ms conversion
    ADS1115_DR_860SPS = 7, // 0b111 -> 860SPS, ~1.2ms conversion
} ads1115_data_rate_t;

/* --------------------------- CONFIG STRUCT ----------------------------*/
/**
 * @brief Per-channel acquisition configuration.
 *
 * Stored per-channel rather than as a single global setting, so each of
 * the 4 input channels can use an independent gain and data rate.
 *
 * @param channel    Input channel this configuration applies to
 * @param pga        Full-scale range for this channel
 * @param data_rate  Conversion speed for this channel
 */
typedef struct {
    ads1115_channel_t   channel;
    ads1115_pga_t       pga;
    ads1115_data_rate_t data_rate;
} ads1115_channel_config_t;

/* --------------------------- RUNTIME STRUCT ----------------------------*/
/**
 * @brief ADS1115 driver instance context.
 *
 * Caller allocates this struct (stack or static). Every vtable method
 * receives a pointer to this struct as its first argument.
 *
 * @param bus             Pointer to the i2c_bus_t used for all transactions
 * @param dev              I2C device handle obtained at init
 * @param channel_config   Per-channel PGA/data-rate settings, copied from config at init
 * @param initialized      Guard flag, set true only after init() succeeds
 */
typedef struct {
    i2c_bus_t               *bus;
    i2c_master_dev_handle_t  dev;
    ads1115_channel_config_t channel_config[4];
    bool                     initialized;
} ads1115_dev_t;

/**
 * @brief Init configuration for ADS1115 driver instance.
 *
 * Passed to init() and not retained after init completes.
 *
 * @param bus             Pointer to an already-initialized i2c_bus_t instance
 * @param addr            7-bit I2C device address (set by ADDR pin strap)
 * @param scl_hz          Per-device I2C clock in Hz, applied when the device is registered
 * @param channel_config  Per-channel PGA/data-rate settings, copied into the runtime context
 */
typedef struct {
    i2c_bus_t               *bus;
    ads1115_addr_t           addr;
    uint32_t                 scl_hz;
    ads1115_channel_config_t channel_config[4];
} ads1115_config_t;

/**
 * @brief ADS1115 driver interface (vtable).
 * Defines the unified contract for all ADS1115 operations. Single access
 * path: callers only ever reach the concrete implementation through these
 * function pointers, obtained via ads1115_get_driver().
 *
 * @param dev     Pointer to the ADS1115 runtime context (ads1115_dev_t)
 * @param cfg     Pointer to the initialization config (ads1115_config_t)
 * @param channel Channel to trigger and read (ads1115_channel_t)
 * @param out_raw Output pointer for raw 16-bit conversion result
 *
 * @return
 * - ESP_OK                : Operation completed successfully
 * - ESP_ERR_INVALID_ARG   : NULL pointer or invalid argument
 * - ESP_ERR_INVALID_STATE : Driver not initialized
 * - ESP_FAIL              : I2C transaction failed
 * - void                  : No return value
 */
typedef struct {
    /**
     * @brief Initialize driver, register I2C device, apply per-channel config
     */
    esp_err_t (*init)(ads1115_dev_t *dev, const ads1115_config_t *cfg);
    /**
     * @brief Trigger single-shot conversion and return raw result
     */
    esp_err_t (*read)(ads1115_dev_t *dev, ads1115_channel_t channel, uint16_t *out_raw);
    /**
     * @brief Write factory default value to config register
     */
    esp_err_t (*reset)(ads1115_dev_t *dev);
    /**
     * @brief De-init all resources held by driver instance
     */
    void (*deinit)(ads1115_dev_t *dev);
} ads1115_driver_t;

// Concrete Driver Access
/**
 * @brief Return pointer to the singleton concrete driver vTable
 */
const ads1115_driver_t *ads1115_get_driver(void);

#ifdef __cplusplus
}
#endif