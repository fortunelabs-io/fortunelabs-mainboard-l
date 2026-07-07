#pragma once

#include "driver/i2c_master.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Device Limitation
#define I2C_BUS_MAX_DEVICES 8
#define I2C_BUS_TIMEOUT_MS 100

// Bus self-healing (see architecture textbook Section 3.3)
#define I2C_BUS_RECOVERY_RETRY 1  // retry attempts per transaction after a successful bus recovery
#define I2C_BUS_RECOVERY_CLOCKS 9 // max SCL pulses issued to force a stuck slave to release SDA

// Device entry registry
typedef struct {
    uint8_t                 addr; // 7 bit I2C address
    i2c_master_dev_handle_t handle;
    const char             *label;
    bool                    active;
} i2c_device_entry_t;

// Bus Device Configuration
typedef struct {
    int        sda_pin;
    int        scl_pin;
    i2c_port_t port;
    uint32_t   clk_hz;
} i2c_bus_config_t;

//* Bus instance
/**
 * @brief Runtime state for one I2C master bus, including its device registry and mutex.
 * sda_pin, scl_pin, and port are retained from i2c_bus_config_t at init time and are used
 * only for bus self-healing (manual GPIO clock bashing and master bus rebuild), never for
 * normal transactions.
 *
 * @param bus_handle    ESP-IDF v5.x I2C master bus handle
 * @param mutex         Serializes all bus access, including recovery
 * @param devices       Registered device table, re-applied after a bus recovery
 * @param device_count  Number of active entries in devices[]
 * @param error_count   Lifetime transaction error count, published in health heartbeat
 * @param sda_pin       SDA GPIO, retained for recovery bitbang
 * @param scl_pin       SCL GPIO, retained for recovery bitbang
 * @param port          I2C port number, retained for recovery bus handle rebuild
 * @param initialized   True once bus_handle is valid and ready for transactions
 */
typedef struct {
    i2c_master_bus_handle_t bus_handle;
    SemaphoreHandle_t       mutex;
    i2c_device_entry_t      devices[I2C_BUS_MAX_DEVICES];
    uint8_t                 device_count;
    uint32_t                error_count;
    int                     sda_pin;
    int                     scl_pin;
    i2c_port_t              port;
    bool                    initialized;
} i2c_bus_t;

// * Lifecycle
// Init the I2CBus
esp_err_t i2c_bus_init(i2c_bus_t *bus, const i2c_bus_config_t *cfg);
// De-initialize bus and release all resoruce
esp_err_t i2c_bus_deinit(i2c_bus_t *bus);

// * Device Management
// Register a device on the bus
/**
 * @param addr      7-bit I2C address (e.g. 0x48 for ADS1115)
 * @param scl_hz    Clock speed for this device
 * @param label     Human-readable name for logging
 * @param out       Receives the device handle for subsequent R/W calls
 */
esp_err_t i2c_bus_add_device(i2c_bus_t *bus, uint8_t addr, uint32_t scl_hz, const char *label,
                             i2c_master_dev_handle_t *out);

//*Data Transfer
// Write data
esp_err_t i2c_bus_write(i2c_bus_t *bus, i2c_master_dev_handle_t dev, const uint8_t *data,
                        size_t len);
// Read data
esp_err_t i2c_bus_read(i2c_bus_t *bus, i2c_master_dev_handle_t dev, uint8_t *buf, size_t len);
// Register read pattern
esp_err_t i2c_bus_write_read(i2c_bus_t *bus, i2c_master_dev_handle_t dev, const uint8_t *wr_data,
                             size_t wr_len, uint8_t *rd_buf, size_t rd_len);

//* Diagnostic
// Scan bus for all responding devices (0x03–0x77).
esp_err_t i2c_bus_scan(i2c_bus_t *bus);
// Publish lifetime error count, publish in health heartbeat
uint32_t i2c_bus_get_error_count(const i2c_bus_t *bus);
// Get num of registered device
uint8_t i2c_bus_get_device_count(const i2c_bus_t *bus);

#ifdef __cplusplus
}
#endif