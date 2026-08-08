// SPDX-License-Identifier: Apache-2.0

/**
 * @file transport_driver.h
 * @brief Transport HAL Contract
 *
 * Hardware abstraction layer contract for outbound communication from
 * this mainboard. Any communication interface implements this contract.
 *
 * Design note: transport does not use the I2C bus (it uses WiFi/radio),
 * so its config shape differs from sensor/display/output drivers.
 */

#pragma once

#include "esp_err.h"
#include "stdbool.h"
#include "stddef.h"
#include "stdint.h"

#ifdef __cplusplus
extern "C" {
#endif

/* --------------------------- COMMAND CALLBACK ----------------------------*/
/**
 * @brief Callback invoked when a control command arrives from the remote broker.
 *
 * @param topic     Endpoint the command came from
 * @param data      Payload bytes
 * @param data_len  Payload length
 *
 * @return void
 */
typedef void(transport_cmd_cb_t)(const char *topic, const uint8_t *data, size_t data_len);

/* --------------------------- CONFIG STRUCT ----------------------------*/
/**
 * @brief Configuration structure for initializing a transport driver instance.
 *
 * Holds connection credentials, device identifiers, and the asynchronous
 * command callback, plus an open pointer for driver-specific extensions.
 *
 * @param broker_uri  Target server address or endpoint URI
 * @param device_id   Unique identifier for this device, typically derived from the MAC address
 * @param cmd_cb      Callback invoked when a remote command is received
 * @param extra       Generic pointer to a custom, driver-specific configuration (nullable)
 */
typedef struct {
    const char         *broker_uri;
    const char         *device_id;
    transport_cmd_cb_t *cmd_cb;
    void               *extra;
} transport_config_t;

/**
 * @brief Transport driver interface (vtable).
 * Defines the unified contract for all transport operations. Single
 * access path: callers only ever reach a concrete transport
 * implementation through these function pointers.
 *
 * @param cfg      Pointer to the transport_config_t structure
 * @param topic    Destination topic or endpoint identifier
 * @param payload  Data content to transmit
 * @param len      Length of payload in bytes
 *
 * @return
 * - ESP_OK   : Operation completed successfully
 * - ESP_FAIL / Specific esp_err_t : Connection failed, timeout, or transmission error
 * - true / false : Boolean connectivity status
 * - void     : No return value
 */
typedef struct {
    const char *name;

    /**
     * @brief Initialize transport, connect to broker or server
     */
    esp_err_t (*init)(const transport_config_t *cfg);
    /**
     * @brief Publish a telemetry payload to the broker
     */
    esp_err_t (*publish)(const char *topic, const char *payload, size_t len);
    /**
     * @brief Subscribe to a command/control topic
     */
    esp_err_t (*subscribe)(const char *topic);
    /**
     * @brief Check whether the transport is connected and ready
     * @return true if connected, false otherwise
     */
    bool (*is_connected)(void);
    /**
     * @brief Disconnect from the network and release allocated resources
     */
    void (*deinit)(void);
} transport_driver_t;

#ifdef __cplusplus
}
#endif