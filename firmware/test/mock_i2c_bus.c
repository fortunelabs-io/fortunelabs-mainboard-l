/**
 * @file mock_i2c_bus.c
 * @brief Fake i2c_bus implementation for SIL testing. See mock_i2c_bus.h.
 */

#include "mock_i2c_bus.h"
#include <string.h>

/* --------------------------- INTERNAL STATE ---------------------------*/
static uint8_t           s_fake_regs8[MOCK_I2C_REG8_COUNT];
static uint16_t          s_fake_regs16[MOCK_I2C_REG16_COUNT];
static mock_i2c_record_t s_log[MOCK_I2C_LOG_MAX];
static uint32_t          s_log_count;

static uint32_t  s_call_counter;
static esp_err_t s_forced_next_result = ESP_OK; // one-shot, fires on the very next call
static uint32_t  s_forced_call_number = 0;      // 0 = none scheduled
static esp_err_t s_forced_call_result = ESP_OK;

static uint32_t s_last_vtaskdelay_ticks;
static uint32_t s_vtaskdelay_call_count;

/* ---------------------------- PRIVATE HELPER -------------------------- */
/**
 * @brief Append one entry to the transaction log if space remains.
 *
 * @param op        Transaction kind
 * @param reg       Register address involved
 * @param val       Byte or word written or read
 * @param is_16bit  true if this transaction used the wide (16-bit) pattern
 *
 * @return void: silently drops the entry past MOCK_I2C_LOG_MAX rather than
 *         overflowing; a test exceeding this ceiling should raise the
 *         constant, not be silently truncated without notice in review.
 */
static void _mock_log_append(mock_i2c_op_t op, uint8_t reg, uint16_t val, bool is_16bit) {
    if (s_log_count >= MOCK_I2C_LOG_MAX) {
        return;
    }
    s_log[s_log_count].op       = op;
    s_log[s_log_count].reg_addr = reg;
    s_log[s_log_count].value    = val;
    s_log[s_log_count].is_16bit = is_16bit;
    s_log_count++;
}

/**
 * @brief Resolve the fault, if any, that applies to the current transfer call.
 *
 * Advances the call counter first, then checks the one-shot "next call"
 * fault before the Nth-call fault. Both are one-shot: each auto-clears
 * once triggered, so a test never needs to remember to un-arm it.
 *
 * @return
 * - ESP_OK  : No fault applies to this call
 * - other   : The fault that was armed for this call, now cleared
 */
static esp_err_t _mock_resolve_fault_for_this_call(void) {
    s_call_counter++;

    if (s_forced_next_result != ESP_OK) {
        esp_err_t ret        = s_forced_next_result;
        s_forced_next_result = ESP_OK;
        return ret;
    }

    if (s_forced_call_number != 0 && s_call_counter == s_forced_call_number) {
        esp_err_t ret        = s_forced_call_result;
        s_forced_call_number = 0;
        return ret;
    }

    return ESP_OK;
}

/* --------------------------- CONTROL API ---------------------------*/
void mock_i2c_bus_reset(void) {
    memset(s_fake_regs8, 0, sizeof(s_fake_regs8));
    memset(s_fake_regs16, 0, sizeof(s_fake_regs16));
    memset(s_log, 0, sizeof(s_log));
    s_log_count = 0;

    s_call_counter       = 0;
    s_forced_next_result = ESP_OK;
    s_forced_call_number = 0;
    s_forced_call_result = ESP_OK;

    s_last_vtaskdelay_ticks = 0;
    s_vtaskdelay_call_count = 0;
}

esp_err_t mock_i2c_bus_set_reg(uint8_t addr, uint8_t value) {
    if (addr >= MOCK_I2C_REG8_COUNT) {
        return ESP_ERR_INVALID_ARG;
    }
    s_fake_regs8[addr] = value;
    return ESP_OK;
}

esp_err_t mock_i2c_bus_get_reg(uint8_t addr, uint8_t *value) {
    if (addr >= MOCK_I2C_REG8_COUNT || value == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    *value = s_fake_regs8[addr];
    return ESP_OK;
}

esp_err_t mock_i2c_bus_set_reg16(uint8_t addr, uint16_t value) {
    if (addr >= MOCK_I2C_REG16_COUNT) {
        return ESP_ERR_INVALID_ARG;
    }
    s_fake_regs16[addr] = value;
    return ESP_OK;
}

esp_err_t mock_i2c_bus_get_reg16(uint8_t addr, uint16_t *value) {
    if (addr >= MOCK_I2C_REG16_COUNT || value == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    *value = s_fake_regs16[addr];
    return ESP_OK;
}

void mock_i2c_bus_force_next_result(esp_err_t err) { s_forced_next_result = err; }

void mock_i2c_bus_force_result_on_call(uint32_t call_number, esp_err_t err) {
    s_forced_call_number = call_number;
    s_forced_call_result = err;
}

uint32_t mock_i2c_bus_get_log_count(void) { return s_log_count; }

const mock_i2c_record_t *mock_i2c_bus_get_log_entry(uint32_t index) {
    if (index >= s_log_count) {
        return NULL;
    }
    return &s_log[index];
}

uint32_t mock_i2c_bus_count_op(mock_i2c_op_t op) {
    uint32_t count = 0;
    for (uint32_t i = 0; i < s_log_count; i++) {
        if (s_log[i].op == op) {
            count++;
        }
    }
    return count;
}

uint32_t mock_get_last_vtaskdelay_ticks(void) { return s_last_vtaskdelay_ticks; }
uint32_t mock_get_vtaskdelay_call_count(void) { return s_vtaskdelay_call_count; }

/* --------------------------- FAKE TRANSFER FUNCTIONS ---------------------------*/
/**
 * @note These replace the real i2c_bus.c transfer functions at link time
 *       for the native test environment only. Signatures match i2c_bus.h
 *       exactly so mcp23017.c and ads1115.c compile unmodified.
 */

esp_err_t i2c_bus_init(i2c_bus_t *bus, const i2c_bus_config_t *cfg) {
    (void)cfg;
    if (bus == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    bus->initialized  = true;
    bus->device_count = 0;
    bus->error_count  = 0;
    return ESP_OK;
}

esp_err_t i2c_bus_deinit(i2c_bus_t *bus) {
    if (bus == NULL || !bus->initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    bus->initialized = false;
    return ESP_OK;
}

esp_err_t i2c_bus_add_device(i2c_bus_t *bus, uint8_t addr, uint32_t scl_hz, const char *label,
                             i2c_master_dev_handle_t *out) {
    (void)scl_hz;
    (void)label;
    if (bus == NULL || out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    // Fake handle: tests only need a non-NULL, stable value to pass through
    // to the transfer functions below, not a real ESP-IDF handle.
    static uint8_t s_fake_handle_storage;
    *out = (i2c_master_dev_handle_t)&s_fake_handle_storage;
    (void)addr;
    bus->device_count++;
    return ESP_OK;
}

esp_err_t i2c_bus_write(i2c_bus_t *bus, i2c_master_dev_handle_t dev, const uint8_t *data,
                        size_t len) {
    (void)bus;
    (void)dev;

    esp_err_t forced = _mock_resolve_fault_for_this_call();
    if (forced != ESP_OK) {
        return forced;
    }
    if (data == NULL || len < 2) {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t reg = data[0];

    if (len == 2) {
        // 8-bit device write pattern: [reg, value] (MCP23017)
        uint8_t val = data[1];
        if (reg < MOCK_I2C_REG8_COUNT) {
            s_fake_regs8[reg] = val;
        }
        _mock_log_append(MOCK_I2C_OP_WRITE, reg, val, false);
        return ESP_OK;
    }

    if (len == 3) {
        // 16-bit device write pattern: [reg, MSB, LSB] (ADS1115)
        uint16_t val = ((uint16_t)data[1] << 8) | data[2];
        if (reg < MOCK_I2C_REG16_COUNT) {
            s_fake_regs16[reg] = val;
        }
        _mock_log_append(MOCK_I2C_OP_WRITE, reg, val, true);
        return ESP_OK;
    }

    return ESP_ERR_INVALID_ARG; // unrecognized payload shape
}

esp_err_t i2c_bus_read(i2c_bus_t *bus, i2c_master_dev_handle_t dev, uint8_t *buf, size_t len) {
    (void)bus;
    (void)dev;

    esp_err_t forced = _mock_resolve_fault_for_this_call();
    if (forced != ESP_OK) {
        return forced;
    }
    if (buf == NULL || len == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    // Not used by mcp23017.c or ads1115.c (both always use write_read for
    // register reads); kept for contract completeness only.
    buf[0] = 0;
    _mock_log_append(MOCK_I2C_OP_READ, 0, buf[0], false);
    return ESP_OK;
}

esp_err_t i2c_bus_write_read(i2c_bus_t *bus, i2c_master_dev_handle_t dev, const uint8_t *wr_data,
                             size_t wr_len, uint8_t *rd_buf, size_t rd_len) {
    (void)bus;
    (void)dev;

    esp_err_t forced = _mock_resolve_fault_for_this_call();
    if (forced != ESP_OK) {
        return forced;
    }
    if (wr_data == NULL || wr_len < 1 || rd_buf == NULL || rd_len < 1) {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t reg = wr_data[0];

    if (rd_len == 1) {
        // 8-bit device read pattern (MCP23017)
        uint8_t val = (reg < MOCK_I2C_REG8_COUNT) ? s_fake_regs8[reg] : 0;
        rd_buf[0]   = val;
        _mock_log_append(MOCK_I2C_OP_WRITE_READ, reg, val, false);
        return ESP_OK;
    }

    if (rd_len == 2) {
        // 16-bit device read pattern (ADS1115), big-endian on the wire
        uint16_t val = (reg < MOCK_I2C_REG16_COUNT) ? s_fake_regs16[reg] : 0;
        rd_buf[0]    = (uint8_t)((val >> 8) & 0xFF);
        rd_buf[1]    = (uint8_t)(val & 0xFF);
        _mock_log_append(MOCK_I2C_OP_WRITE_READ, reg, val, true);
        return ESP_OK;
    }

    return ESP_ERR_INVALID_ARG; // unrecognized payload shape
}

esp_err_t i2c_bus_scan(i2c_bus_t *bus) {
    if (bus == NULL || !bus->initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    return ESP_OK;
}

uint32_t i2c_bus_get_error_count(const i2c_bus_t *bus) {
    return (bus != NULL) ? bus->error_count : 0;
}

uint8_t i2c_bus_get_device_count(const i2c_bus_t *bus) {
    return (bus != NULL) ? bus->device_count : 0;
}

/* --------------------------- FREERTOS STAND-IN ---------------------------*/
/**
 * @note vTaskDelay is tracked, not executed: SIL asserts the requested
 *       duration (via mock_get_last_vtaskdelay_ticks), not that time
 *       actually passed. A real sleep would only slow the suite for no
 *       verification benefit.
 */
void vTaskDelay(uint32_t xTicksToDelay) {
    s_last_vtaskdelay_ticks = xTicksToDelay;
    s_vtaskdelay_call_count++;
}