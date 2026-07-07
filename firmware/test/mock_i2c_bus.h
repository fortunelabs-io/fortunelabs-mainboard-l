/**
 * @file mock_i2c_bus.h
 * @brief Fake i2c_bus for SIL (host-native) testing.
 *
 * Drop-in substitute for the real i2c_bus.c. Implements the same three
 * transfer functions (i2c_bus_write, i2c_bus_read, i2c_bus_write_read)
 * used by every IC driver, backed by in-RAM register files instead of a
 * physical bus. Drivers under test (mcp23017.c, ads1115.c) are compiled
 * completely unmodified against this substitute.
 *
 * Device width is resolved per-call from the transfer shape, not from
 * any device identity: a 2-byte i2c_bus_write is treated as an 8-bit
 * register device (MCP23017 pattern: [reg, value]); a 3-byte write is
 * treated as a 16-bit device (ADS1115 pattern: [reg, MSB, LSB]). The
 * two widths are backed by separate register files, so there is no
 * possibility of address collision between driver types under test.
 *
 * Also stands in for the two FreeRTOS calls ads1115.c depends on
 * (vTaskDelay / pdMS_TO_TICKS): the delay is recorded, not executed, so
 * tests can assert the requested duration without actually sleeping.
 * This broadens the file's scope slightly beyond literal I2C; kept here
 * rather than a separate mock_freertos.c to avoid an extra file for a
 * single tracked call.
 *
 * Scope: register-level logic only. Electrical timing, real ACK
 * behavior, and concurrency are explicitly out of scope; see
 * SIL_TEST_REPORT.md Section 5 for the full list of known gaps.
 */

#pragma once

#include "bus/i2c_bus.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MOCK_I2C_REG8_COUNT 32 ///< Covers MCP23017 registers (0x00-0x15) with headroom
#define MOCK_I2C_REG16_COUNT 8 ///< Covers ADS1115 registers (0x00-0x03) with headroom
#define MOCK_I2C_LOG_MAX 64    ///< Max recorded transactions per test; reset() clears the log

/* --------------------------- TRANSACTION LOG ---------------------------*/
/**
 * @brief One recorded bus transaction, in the order it occurred.
 *
 * A write_read counts as ONE entry of kind MOCK_I2C_OP_WRITE_READ, not two
 * separate write/read entries, so a test can assert repeated-start
 * behavior distinctly from a plain write followed by a plain read.
 */
typedef enum {
    MOCK_I2C_OP_WRITE = 0,
    MOCK_I2C_OP_READ,
    MOCK_I2C_OP_WRITE_READ,
} mock_i2c_op_t;

typedef struct {
    mock_i2c_op_t op;
    uint8_t       reg_addr; ///< First byte of the write payload (the register pointer)
    uint16_t      value;    ///< Byte or word written/read; 8-bit values sit in the low byte
    bool          is_16bit; ///< true if this transaction used the 3-byte/2-byte-read wide pattern
} mock_i2c_record_t;

/* --------------------------- CONTROL API ---------------------------*/
/**
 * @brief Test harness control surface for the fake bus.
 *
 * @param addr        Register address in the relevant fake register file
 * @param value       Byte or word to seed into or read back from the register file
 * @param err         Forced esp_err_t to return from a transfer call
 * @param call_number 1-indexed transfer call to target (counts write + read +
 *                    write_read together, in call order, since the last reset())
 *
 * @return
 * - ESP_OK              : Operation completed successfully
 * - ESP_ERR_INVALID_ARG : addr out of range
 * - void                : No return value
 */

/** @brief Clear both register files, the transaction log, all forced faults, and
 *         the vTaskDelay tracking counters back to defaults */
void mock_i2c_bus_reset(void);

/** @brief Seed the 8-bit (MCP23017-style) fake register file at addr with value */
esp_err_t mock_i2c_bus_set_reg(uint8_t addr, uint8_t value);
/** @brief Read back the 8-bit fake register file at addr */
esp_err_t mock_i2c_bus_get_reg(uint8_t addr, uint8_t *value);

/** @brief Seed the 16-bit (ADS1115-style) fake register file at addr with value */
esp_err_t mock_i2c_bus_set_reg16(uint8_t addr, uint16_t value);
/** @brief Read back the 16-bit fake register file at addr */
esp_err_t mock_i2c_bus_get_reg16(uint8_t addr, uint16_t *value);

/** @brief Force the very next transfer call to return err instead of ESP_OK, then
 *         auto-clear. Use for single-call-deep failure scenarios. */
void mock_i2c_bus_force_next_result(esp_err_t err);

/** @brief Force the Nth transfer call (1-indexed, since the last reset()) to
 *         return err instead of ESP_OK, then auto-clear. Use when a scenario
 *         needs the SECOND (or later) of several internal calls to fail,
 *         e.g. asserting that a config write succeeds but the following
 *         conversion read fails. */
void mock_i2c_bus_force_result_on_call(uint32_t call_number, esp_err_t err);

/** @brief Total number of transactions recorded since the last reset() */
uint32_t mock_i2c_bus_get_log_count(void);

/** @brief Retrieve the transaction log entry at the given index (0-indexed, chronological) */
const mock_i2c_record_t *mock_i2c_bus_get_log_entry(uint32_t index);

/** @brief Convenience: count of entries in the log matching a specific op kind */
uint32_t mock_i2c_bus_count_op(mock_i2c_op_t op);

/* --------------------------- FREERTOS TRACKING ---------------------------*/
/** @brief Ticks requested in the most recent vTaskDelay() call; 0 if none yet */
uint32_t mock_get_last_vtaskdelay_ticks(void);
/** @brief Number of vTaskDelay() calls made since the last reset() */
uint32_t mock_get_vtaskdelay_call_count(void);

#ifdef __cplusplus
}
#endif