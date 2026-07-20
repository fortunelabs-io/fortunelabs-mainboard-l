#include "bus/i2c_bus.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/task.h"
#include <string.h>

static const char *TAG = "i2c_bus";

// Timing for manual clock bashing, matches standard I2C bit period at 100kHz.
// Slow and conservative on purpose: recovery is rare and correctness matters
// more than speed here.
#define I2C_BUS_RECOVERY_HALF_PERIOD_US 5

// * Lifecycle

// Bus Initialization
esp_err_t i2c_bus_init(i2c_bus_t *bus, const i2c_bus_config_t *cfg) {
    if (bus == NULL || cfg == NULL)
        return ESP_ERR_INVALID_ARG;

    memset(bus, 0, sizeof(i2c_bus_t));

    // Create mutex
    bus->mutex = xSemaphoreCreateMutex();
    if (bus->mutex == NULL) {
        ESP_LOGE(TAG, "Failed to create mutex");
        return ESP_ERR_NO_MEM;
    }

    // Configuring ESP IDF 5.x.x I2C master bus
    i2c_master_bus_config_t bus_cfg = {
        .clk_source                   = I2C_CLK_SRC_DEFAULT,
        .i2c_port                     = cfg->port,
        .scl_io_num                   = cfg->scl_pin,
        .sda_io_num                   = cfg->sda_pin,
        .glitch_ignore_cnt            = 7,
        .flags.enable_internal_pullup = true, // ! True if doesnt have resistor
    };

    esp_err_t ret = i2c_new_master_bus(&bus_cfg, &bus->bus_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "i2c_new_master_bus failed: %s", esp_err_to_name(ret));
        vSemaphoreDelete(bus->mutex);
        return ret;
    }

    // Retained for bus self-healing only (see _i2c_bus_recover_locked), never
    // used for normal transactions.
    bus->sda_pin = cfg->sda_pin;
    bus->scl_pin = cfg->scl_pin;
    bus->port    = cfg->port;

    bus->initialized = true;
    ESP_LOGI(TAG, "Bus init OK, port=%d SDA=%d SCL=%d clk=%luHz", cfg->port, cfg->sda_pin,
             cfg->scl_pin, (unsigned long)cfg->clk_hz);

    return ESP_OK;
}

// Bus De-Initialization
esp_err_t i2c_bus_deinit(i2c_bus_t *bus) {
    if (bus == NULL || !bus->initialized)
        return ESP_ERR_INVALID_STATE;

    esp_err_t ret = i2c_del_master_bus(bus->bus_handle);
    if (bus->mutex)
        vSemaphoreDelete(bus->mutex);

    bus->initialized = false;
    ESP_LOGI(TAG, "Bus has been de-initialized");
    return ret;
}

//* Device Management
esp_err_t i2c_bus_add_device(i2c_bus_t *bus, uint8_t addr, uint32_t scl_hz, const char *label,
                             i2c_master_dev_handle_t *out) {
    if (bus == NULL || !bus->initialized || out == NULL)
        return ESP_ERR_INVALID_ARG;
    if (bus->device_count >= I2C_BUS_MAX_DEVICES) {
        ESP_LOGE(TAG, "Device registry full (%d/%d)", bus->device_count, I2C_BUS_MAX_DEVICES);
        return ESP_ERR_NO_MEM;
    }

    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address  = addr,
        .scl_speed_hz    = scl_hz,
    };

    esp_err_t ret = i2c_master_bus_add_device(bus->bus_handle, &dev_cfg, out);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to add device 0x%02X (%s): %s", addr, label ? label : "?",
                 esp_err_to_name(ret));
        return ret;
    }

    // Register in our device table
    i2c_device_entry_t *entry = &bus->devices[bus->device_count];
    entry->addr               = addr;
    entry->handle             = *out;
    entry->label              = label;
    entry->active             = true;
    bus->device_count++;

    ESP_LOGI(TAG, "Device added: 0x%02X (%s), - %lu Hz [%d/%d slots]", addr, label ? label : "?",
             (unsigned long)scl_hz, bus->device_count, I2C_BUS_MAX_DEVICES);

    return ESP_OK;
}

//* Bus self-healing (architecture textbook Section 3.3)
//
// Internal only. Not declared in i2c_bus.h.
//
// Contract: caller must already hold bus->mutex before calling this function.
// This function does not take or give the mutex itself. It is called from
// inside i2c_bus_write/read/write_read while their own mutex hold is still
// active, so no other task can observe or interrupt the bus mid-rebuild.
//
// This is a fixed, fully specified physical-layer procedure: force any slave
// holding SDA low to release it by clocking SCL manually, issue a STOP
// condition, then tear down and rebuild the master bus handle and reapply
// the device registry. It carries no judgment about what the fault means for
// the system; that decision stays with the caller and, beyond the retry
// budget below, with the supervisor watching error_count.
static esp_err_t _i2c_bus_recover_locked(i2c_bus_t *bus) {
    ESP_LOGW(TAG, "Bus recovery starting (SDA=%d SCL=%d)", bus->sda_pin, bus->scl_pin);

    // Tear down the wedged master bus handle before touching the pins directly,
    // so the driver is not holding the same lines we are about to bitbang.
    if (bus->bus_handle != NULL) {
        i2c_del_master_bus(bus->bus_handle);
        bus->bus_handle = NULL;
    }
    bus->initialized = false;

    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << bus->sda_pin) | (1ULL << bus->scl_pin),
        .mode         = GPIO_MODE_INPUT_OUTPUT_OD,
        .pull_up_en   = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    gpio_config(&io_conf);

    gpio_set_level(bus->scl_pin, 1);
    gpio_set_level(bus->sda_pin, 1);
    esp_rom_delay_us(I2C_BUS_RECOVERY_HALF_PERIOD_US);

    // Clock SCL until SDA is released or the pulse budget is exhausted.
    // Early exit on release, rather than a blind fixed count, so a slave that
    // frees the line after one or two pulses is not held through the rest.
    int pulses = 0;
    while (gpio_get_level(bus->sda_pin) == 0 && pulses < I2C_BUS_RECOVERY_CLOCKS) {
        gpio_set_level(bus->scl_pin, 0);
        esp_rom_delay_us(I2C_BUS_RECOVERY_HALF_PERIOD_US);
        gpio_set_level(bus->scl_pin, 1);
        esp_rom_delay_us(I2C_BUS_RECOVERY_HALF_PERIOD_US);
        pulses++;
    }

    if (gpio_get_level(bus->sda_pin) == 0) {
        ESP_LOGE(TAG, "Bus recovery failed: SDA still LOW after %d clocks", pulses);
        return ESP_ERR_INVALID_STATE;
    }

    // Manual STOP condition: SDA rises while SCL is HIGH.
    gpio_set_level(bus->sda_pin, 0);
    esp_rom_delay_us(I2C_BUS_RECOVERY_HALF_PERIOD_US);
    gpio_set_level(bus->scl_pin, 1);
    esp_rom_delay_us(I2C_BUS_RECOVERY_HALF_PERIOD_US);
    gpio_set_level(bus->sda_pin, 1);
    esp_rom_delay_us(I2C_BUS_RECOVERY_HALF_PERIOD_US);

    ESP_LOGI(TAG, "Bus line released after %d clock pulse(s), rebuilding master bus", pulses);

    // Rebuild the master bus handle from the retained pin/port configuration.
    i2c_master_bus_config_t bus_cfg = {
        .clk_source                   = I2C_CLK_SRC_DEFAULT,
        .i2c_port                     = bus->port,
        .scl_io_num                   = bus->scl_pin,
        .sda_io_num                   = bus->sda_pin,
        .glitch_ignore_cnt            = 7,
        .flags.enable_internal_pullup = true,
    };

    esp_err_t ret = i2c_new_master_bus(&bus_cfg, &bus->bus_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Bus recovery: failed to rebuild master bus: %s", esp_err_to_name(ret));
        return ret;
    }

    // Reapply the device registry against the new bus handle. Abort on the
    // first failure rather than continuing with a partially-restored registry:
    // a bus that is clearly down is safer for the caller to reason about than
    // one where some devices silently lost their handle.
    for (uint8_t i = 0; i < bus->device_count; i++) {
        i2c_device_entry_t *entry = &bus->devices[i];

        i2c_device_config_t dev_cfg = {
            .dev_addr_length = I2C_ADDR_BIT_LEN_7,
            .device_address  = entry->addr,
            .scl_speed_hz    = 0, // per-device speed not retained; see open item below
        };

        ret = i2c_master_bus_add_device(bus->bus_handle, &dev_cfg, &entry->handle);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Bus recovery: failed to re-add 0x%02X (%s): %s", entry->addr,
                     entry->label ? entry->label : "?", esp_err_to_name(ret));
            i2c_del_master_bus(bus->bus_handle);
            bus->bus_handle = NULL;
            return ret;
        }
    }

    bus->initialized = true;
    ESP_LOGI(TAG, "Bus recovery complete, %d device(s) reattached", bus->device_count);
    return ESP_OK;
}

// * Mutex - Data transfer
// Bus write operation
esp_err_t i2c_bus_write(i2c_bus_t *bus, i2c_master_dev_handle_t dev, const uint8_t *data,
                        size_t len) {
    if (bus == NULL || !bus->initialized)
        return ESP_ERR_INVALID_STATE;

    if (!xSemaphoreTake(bus->mutex, pdMS_TO_TICKS(I2C_BUS_TIMEOUT_MS))) {
        bus->error_count++;
        ESP_LOGE(TAG, "Bus mutex timeout");
        return ESP_ERR_TIMEOUT;
    }

    esp_err_t ret = i2c_master_transmit(dev, data, len, I2C_BUS_TIMEOUT_MS);

    if (ret == ESP_ERR_TIMEOUT) {
        bus->error_count++;
        ESP_LOGW(TAG, "Write timeout, attempting bus recovery (errors: %lu)",
                 (unsigned long)bus->error_count);

        if (_i2c_bus_recover_locked(bus) == ESP_OK) {
            for (int attempt = 0; attempt < I2C_BUS_RECOVERY_RETRY && ret != ESP_OK; attempt++) {
                ret = i2c_master_transmit(dev, data, len, I2C_BUS_TIMEOUT_MS);
            }
        }
    }

    xSemaphoreGive(bus->mutex);

    if (ret != ESP_OK) {
        bus->error_count++;
        ESP_LOGW(TAG, "Write failed %s (errors: %lu)", esp_err_to_name(ret),
                 (unsigned long)bus->error_count);
    }
    return ret;
}

// Bus read operation
esp_err_t i2c_bus_read(i2c_bus_t *bus, i2c_master_dev_handle_t dev, uint8_t *buf, size_t len) {
    if (bus == NULL || !bus->initialized)
        return ESP_ERR_INVALID_STATE;

    if (!xSemaphoreTake(bus->mutex, pdMS_TO_TICKS(I2C_BUS_TIMEOUT_MS))) {
        bus->error_count++;
        ESP_LOGE(TAG, "Bus mutex timeout");
        return ESP_ERR_TIMEOUT;
    }

    esp_err_t ret = i2c_master_transmit(dev, buf, len, I2C_BUS_TIMEOUT_MS);

    if (ret == ESP_ERR_TIMEOUT) {
        bus->error_count++;
        ESP_LOGW(TAG, "Read timeout, attempting bus recovery (errors: %lu)",
                 (unsigned long)bus->error_count);

        if (_i2c_bus_recover_locked(bus) == ESP_OK) {
            for (int attempt = 0; attempt < I2C_BUS_RECOVERY_RETRY && ret != ESP_OK; attempt++) {
                ret = i2c_master_transmit(dev, buf, len, I2C_BUS_TIMEOUT_MS);
            }
        }
    }

    xSemaphoreGive(bus->mutex);

    if (ret != ESP_OK) {
        bus->error_count++;
        ESP_LOGW(TAG, "Read failed: %s (errors: %lu)", esp_err_to_name(ret),
                 (unsigned long)bus->error_count);
    }
    return ret;
}

// Read write cycle
esp_err_t i2c_bus_write_read(i2c_bus_t *bus, i2c_master_dev_handle_t dev, const uint8_t *wr_data,
                             size_t wr_len, uint8_t *rd_buf, size_t rd_len) {
    if (bus == NULL || !bus->initialized)
        return ESP_ERR_INVALID_STATE;

    if (!xSemaphoreTake(bus->mutex, pdMS_TO_TICKS(I2C_BUS_TIMEOUT_MS))) {
        bus->error_count++;
        ESP_LOGE(TAG, "Bus mutex timeout");
        return ESP_ERR_TIMEOUT;
    }

    esp_err_t ret =
        i2c_master_transmit_receive(dev, wr_data, wr_len, rd_buf, rd_len, I2C_BUS_TIMEOUT_MS);

    if (ret == ESP_ERR_TIMEOUT) {
        bus->error_count++;
        ESP_LOGW(TAG, "Write-read timeout, attempting bus recovery (errors: %lu)",
                 (unsigned long)bus->error_count);

        if (_i2c_bus_recover_locked(bus) == ESP_OK) {
            for (int attempt = 0; attempt < I2C_BUS_RECOVERY_RETRY && ret != ESP_OK; attempt++) {
                ret = i2c_master_transmit_receive(dev, wr_data, wr_len, rd_buf, rd_len,
                                                  I2C_BUS_TIMEOUT_MS);
            }
        }
    }

    xSemaphoreGive(bus->mutex);

    if (ret != ESP_OK) {
        bus->error_count++;
        ESP_LOGW(TAG, "Write-read failed: %s (errors: %lu)", esp_err_to_name(ret),
                 (unsigned long)bus->error_count);
    }
    return ret;
}

//* Diagnostic
esp_err_t i2c_bus_scan(i2c_bus_t *bus) {
    if (bus == NULL || !bus->initialized)
        return ESP_ERR_INVALID_STATE;

    ESP_LOGI(TAG, "===== I2C Bus Scan =====");
    int found = 0;

    if (!xSemaphoreTake(bus->mutex, pdMS_TO_TICKS(I2C_BUS_TIMEOUT_MS))) {
        bus->error_count++;
        ESP_LOGE(TAG, "Bus mutex timeout");
        return ESP_ERR_TIMEOUT;
    }
    for (uint8_t addr = 0x03; addr <= 0x77; addr++) {
        // Try to sending zero bytes for probing
        esp_err_t ret = i2c_master_probe(bus->bus_handle, addr, I2C_BUS_TIMEOUT_MS);

        if (ret == ESP_OK) {
            const char *known = "unknown";
            for (int i = 0; i < bus->device_count; i++) {
                if (bus->devices[i].addr == addr && bus->devices[i].label) {
                    known = bus->devices[i].label;
                    break;
                }
            }
            ESP_LOGI(TAG, "0x%02X - ACK (%s)", addr, known);
            found++;
        }
    }

    xSemaphoreGive(bus->mutex);

    ESP_LOGI(TAG, "===== Scan Completed, %d Device(s) Found =====", found);
    return ESP_OK;
}

uint32_t i2c_bus_get_error_count(const i2c_bus_t *bus) { return bus ? bus->error_count : 0; }

uint8_t i2c_bus_get_device_count(const i2c_bus_t *bus) { return bus ? bus->device_count : 0; }