// SPDX-License-Identifier: Apache-2.0

/**
 * @file mcp23017.h
 * @brief MCP23017 I2C GPIO Expander Driver
 *
 * Driver for the Microchip MCP23017 16-bit bidirectional I/O expander.
 * Communicates over I2C using the platform i2c_bus abstraction layer.
 *
 * Register map is locked to IOCON.BANK = 0 (default sequential addressing).
 * This is enforced at init() and must not be changed at runtime.
 *
 * Shadow RAM strategy:
 *   - dir_a / dir_b   : mirrors IODIRA / IODIRB, enables RMW on pin direction
 *                        without an I2C read.
 *   - olat_a / olat_b : mirrors OLATA / OLATB, enables RMW on single pin
 *                        writes and toggle without an I2C read.
 *
 * Interrupt registers (GPINTEN, DEFVAL, INTCON, INTF, INTCAP) are
 * not exposed in this driver phase.
 *
 * Access pattern: vtable (mcp23017_driver_t) obtained via
 * mcp23017_get_driver(). All operations are reached through the
 * function pointers in this vtable; there are no other public entry
 * points into the driver.
 *
 * @note All vtable methods except init() return ESP_ERR_INVALID_STATE if
 *       called before init() completes successfully.
 */

#pragma once

#include "bus/i2c_bus.h"
#include <esp_err.h>
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------ REGIGISTER MAP ---------------------------------*/
#define MCP23017_REG_IODIRA 0x00      ///< I/O Direction Register for Port A (1=input, 0=output)
#define MCP23017_REG_IODIRB 0x01      ///< I/O Direction Register for Port B (1=input, 0=output)
#define MCP23017_REG_IPOLA 0x02       ///< Input Polarity Register for Port A (1=inverted)
#define MCP23017_REG_IPOLB 0x03       ///< Input Polarity Register for Port B (1=inverted)
#define MCP23017_REG_GPPUA 0x0C       ///< Pull-Up Resistor Register for Port A (1=enabled)
#define MCP23017_REG_GPPUB 0x0D       ///< Pull-Up Resistor Register for Port B (1=enabled)
#define MCP23017_REG_GPIOA 0x12       ///< GPIO Port Register for Port A (read reflects pin state)
#define MCP23017_REG_GPIOB 0x13       ///< GPIO Port Register for Port B (read reflects pin state)
#define MCP23017_REG_OLATA 0x14       ///< Output Latch Register for Port A (read reflects latch)
#define MCP23017_REG_OLATB 0x15       ///< Output Latch Register for Port B (read reflects latch)
#define MCP23017_REG_IOCON 0x0A       ///< I/O Expander Configuration Register
#define MCP23017_REG_IOCON_BANK1 0x05 ///< IOCON address under BANK=1 addressing

/* -------------------------- IOCON BANKS ---------------------------------*/
#define MCP23017_IOCON_BANK (1 << 7)   ///< Register addressing mode (0 = BANK 0, sequential)
#define MCP23017_IOCON_MIRROR (1 << 6) ///< INT pin mirror (1 = INTA/INTB internally connected)
#define MCP23017_IOCON_SEQOP (1 << 5)  ///< Sequential operation (1 = disabled)
#define MCP23017_IOCON_ODR (1 << 2)    ///< INT pin as open-drain output (1 = open-drain)
#define MCP23017_IOCON_INTPOL (1 << 1) ///< INT pin polarity (1 = active-high, 0 = active-low)

/* -------------------------- CONSTANT VARIABLE --------------------------*/
#define MCP23017_ADDR_BASE 0x20 ///< Base I2C address (A2=0, A1=0, A0=0)
#define MCP23017_ADDR_MAX 0x27  ///< Maximum I2C address (A2=1, A1=1, A0=1)
#define MCP23017_PIN_MAX 7      ///< Maximum pin index per port (0–7)

/* --------------------------- POWER ON RESET ----------------------------*/
#define MCP23017_DEFAULT_IODIR 0xFF ///< All pins input on power-on (datasheet default)
#define MCP23017_DEFAULT_OLAT 0x00  ///< All output latches low on power-on

/* --------------------------- ENUMERATION ----------------------------*/
/**
 * @brief Port selection for port-level and pin-level operations.
 */
typedef enum {
    MCP23017_PORT_A = 0, // GPA0-GPA7
    MCP23017_PORT_B = 1, // GPB0-GPB7
} mcp23017_port_t;

/**
 * @brief Pin direction setting for IODIR register.
 */
typedef enum {
    MCP23017_DIR_OUTPUT = 0,
    MCP23017_DIR_INPUT  = 1,
} mcp23017_direction_t;

/* --------------------------- CONFIG STRUCT ----------------------------*/
/**
 * @brief Init configuration for MCP23017 driver instance.
 *
 * Passed to init() and not retained after init completes.
 * All register values written to hardware during init sequence.
 *
 * @param bus       Pointer to an already-initialized i2c_bus_t instance
 * @param address   7-bit I2C device address (MCP23017_ADDR_BASE to MCP23017_ADDR_MAX)
 * @param dir_a     Initial IODIRA value -> bitmask, 1=input, 0=output (default: 0xFF)
 * @param dir_b     Initial IODIRB value -> bitmask, 1=input, 0=output (default: 0xFF)
 * @param pullup_a  Initial GPPUA value  -> bitmask, 1=pull-up enabled (default: 0x00)
 * @param pullup_b  Initial GPPUB value  -> bitmask, 1=pull-up enabled (default: 0x00)
 */
typedef struct {
    i2c_bus_t *bus;
    uint8_t    address;
    uint32_t   scl_hz;
    uint8_t    dir_a;
    uint8_t    dir_b;
    uint8_t    pullup_a;
    uint8_t    pullup_b;
} mcp23017_config_t;

/* --------------------------- RUNTIME STRUCT ----------------------------*/
/**
 * @brief MCP23017 driver instance context.
 *
 * Caller allocates this struct (stack or static). Every vtable method
 * receives a pointer to this struct as its first argument.
 *
 * Shadow fields (dir_a, dir_b, olat_a, olat_b) are kept in sync with
 * the hardware registers at all times by the driver. They enable
 * read-modify-write on single pins without issuing an I2C read transaction.
 *
 * @param bus_handle     Pointer to the i2c_bus_t used for all transactions
 * @param dev_addr       7-bit I2C address of this device instance
 * @param is_initialized Guard flag, set true only after init() succeeds
 * @param dir_a          Shadow of IODIRA register
 * @param dir_b          Shadow of IODIRB register
 * @param olat_a         Shadow of OLATA register
 * @param olat_b         Shadow of OLATB register
 */
typedef struct {
    i2c_bus_t              *bus_handle;
    i2c_master_dev_handle_t dev_handle;
    uint8_t                 dev_addr;
    bool                    is_initialized;
    uint8_t                 dir_a;
    uint8_t                 dir_b;
    uint8_t                 olat_a;
    uint8_t                 olat_b;
} mcp23017_t;

/**
 * @brief MCP23017 driver interface (vtable).
 * Defines the unified contract for all MCP23017 operations. Single access
 * path: callers only ever reach the concrete implementation through these
 * function pointers, obtained via mcp23017_get_driver().
 *
 * @param dev           Pointer to the MCP23017 runtime context (mcp23017_t)
 * @param cfg           Pointer to the initialization config (mcp23017_config_t)
 * @param port          Target port (MCP23017_PORT_A or MCP23017_PORT_B)
 * @param pin           Pin index within the port (0–7, MCP23017_PIN_MAX)
 * @param dir_mask      Bit=1 configures pin as input, bit=0 as output
 * @param direction     MCP23017_DIR_INPUT or MCP23017_DIR_OUTPUT
 * @param value         Port-level: bit=1 drives pin HIGH, bit=0 drives pin LOW.
 *                      Pin-level: true = drive HIGH, false = drive LOW
 * @param pullup_mask   Bit=1 enables pull-up, bit=0 disables
 * @param polarity_mask Bit=1 inverts input, bit=0 no inversion
 * @param output_value  Receives the GPIO register value for the port
 * @param out_value     Receives the live logic state of a single pin
 *
 * @return
 * - ESP_OK                : Operation completed successfully
 * - ESP_ERR_INVALID_ARG   : NULL pointer, invalid port/pin, or invalid enum value
 * - ESP_ERR_INVALID_STATE : Driver not initialized
 * - ESP_FAIL              : I2C transaction failed (IC did not ACK)
 * - void                  : No return value
 */
typedef struct {
    /**
     * @brief Initialize driver, register I2C device, apply direction/pullup/OLAT config
     */
    esp_err_t (*init)(mcp23017_t *dev, const mcp23017_config_t *cfg);
    /**
     * @brief Clear context struct; issues no I2C transactions, hardware state is left as-is
     */
    void (*deinit)(mcp23017_t *dev);

    /**
     * @brief Set direction for all pins on a port simultaneously
     */
    esp_err_t (*set_port_direction)(mcp23017_t *dev, mcp23017_port_t port, uint8_t dir_mask);
    /**
     * @brief Write logic levels to all pins on a port simultaneously (output pins only)
     */
    esp_err_t (*write_port)(mcp23017_t *dev, mcp23017_port_t port, uint8_t value);
    /**
     * @brief Read the live logic state of all pins on a port (GPIO register, not OLAT)
     */
    esp_err_t (*read_port)(mcp23017_t *dev, mcp23017_port_t port, uint8_t *output_value);
    /**
     * @brief Configure internal weak pull-up resistors for all pins on a port
     */
    esp_err_t (*set_port_pullup)(mcp23017_t *dev, mcp23017_port_t port, uint8_t pullup_mask);
    /**
     * @brief Configure input polarity inversion for all pins on a port
     */
    esp_err_t (*set_port_polarity)(mcp23017_t *dev, mcp23017_port_t port, uint8_t polarity_mask);

    /**
     * @brief Configure direction of a single pin (RMW on shadow, zero I2C reads)
     */
    esp_err_t (*set_pin_direction)(mcp23017_t *dev, mcp23017_port_t port, uint8_t pin,
                                   mcp23017_direction_t direction);
    /**
     * @brief Set a single output pin HIGH or LOW (RMW on shadow, zero I2C reads)
     */
    esp_err_t (*write_pin)(mcp23017_t *dev, mcp23017_port_t port, uint8_t pin, bool value);
    /**
     * @brief Read the live logic state of a single pin
     */
    esp_err_t (*read_pin)(mcp23017_t *dev, mcp23017_port_t port, uint8_t pin, bool *out_value);
    /**
     * @brief Invert the current logic state of a single output pin (XOR on shadow, zero I2C reads)
     */
    esp_err_t (*toggle_pin)(mcp23017_t *dev, mcp23017_port_t port, uint8_t pin);
} mcp23017_driver_t;

// Concrete Driver Access
/**
 * @brief Return pointer to the singleton concrete driver vTable
 */
const mcp23017_driver_t *mcp23017_get_driver(void);

#ifdef __cplusplus
}
#endif