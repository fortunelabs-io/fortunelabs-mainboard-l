// SPDX-License-Identifier: Apache-2.0

/**
 * @file network_manager.h
 * @brief WiFi Station and MQTT Network Manager
 *
 * Resilient network layer for ESP32: brings up the TCP/IP stack and WiFi
 * station mode, connects to an MQTT broker, and auto-reconnects on
 * disconnect for both WiFi and MQTT. Not a vtable-based driver; there is
 * exactly one network implementation, accessed via direct function calls.
 *
 * Owns three MQTT topics per device, derived from device_id at init:
 * telemetry (periodic sensor publish), command (inbound control,
 * subscribed on connect), and health (outbound status, including LWT).
 */

#pragma once

#include "esp_err.h"
#include "hal/transport_driver.h"
#include "system/system_config.h"
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* --------------------------- LIFECYCLE ----------------------------*/
/**
 * @brief Initialize the network manager: TCP/IP stack, WiFi station, MQTT client.
 *
 * Does not connect. Brings up esp_netif and the default event loop,
 * configures WiFi station mode from cfg, derives the telemetry/command/
 * health topic strings from cfg->device_id, and constructs the MQTT
 * client (including its Last Will and Testament on the health topic).
 * Call network_manager_start() afterward to begin connecting.
 *
 * @param cfg  Pointer to the system configuration structure
 *
 * @return
 * - ESP_OK              : Initialization completed successfully
 * - ESP_ERR_INVALID_ARG : cfg is NULL
 * - ESP_ERR_NO_MEM      : Status mutex allocation failed
 * - ESP_FAIL            : MQTT client construction failed
 */
esp_err_t network_manager_init(const system_config_t *cfg);

/**
 * @brief Start the WiFi radio and the telemetry publish task.
 *
 * MQTT connection itself is started from the WiFi IP-acquired event
 * handler, not directly by this call.
 *
 * @return
 * - ESP_OK         : WiFi started and telemetry task created
 * - ESP_ERR_NO_MEM : Telemetry task creation failed
 */
esp_err_t network_manager_start(void);

/* --------------------------- STATUS ----------------------------*/
/**
 * @brief Check whether the MQTT client is currently connected to the broker.
 *
 * @return true if connected, false otherwise (including before init)
 */
bool network_manager_is_connected(void);

/* --------------------------- PUBLISH ----------------------------*/
/**
 * @brief Publish a pre-built JSON payload to the device's health topic.
 *
 * Intended for callers outside this module, e.g. the system supervisor
 * publishing a periodic heartbeat.
 *
 * @param json_payload  JSON string payload to publish
 * @param length        Length of json_payload in bytes
 *
 * @return
 * - ESP_OK                : Publish accepted by the MQTT client
 * - ESP_ERR_INVALID_STATE : MQTT client not initialized or not connected
 * - ESP_FAIL              : Underlying MQTT publish call failed
 */
esp_err_t network_manager_publish_health(const char *json_payload, size_t length);

/**
 * @brief Publish a payload to an arbitrary topic on the broker.
 *
 * @param topic    Destination topic
 * @param payload  Payload bytes
 * @param length   Payload length in bytes
 *
 * @return
 * - ESP_OK                : Publish accepted by the MQTT client
 * - ESP_ERR_INVALID_STATE : MQTT client not initialized or not connected
 * - ESP_FAIL              : Underlying MQTT publish call failed
 */
esp_err_t network_manager_publish(const char *topic, const char *payload, size_t length);

/**
 * @brief Subscribe to a topic on the broker.
 *
 * @param topic  Topic to subscribe to
 *
 * @return
 * - ESP_OK                : Subscribe request accepted
 * - ESP_ERR_INVALID_STATE : MQTT client not initialized or not connected
 * - ESP_FAIL              : Underlying MQTT subscribe call failed
 */
esp_err_t network_manager_subscribe(const char *topic);

/**
 * @brief Register the callback invoked when an inbound broker command arrives.
 *
 * When set, inbound MQTT command payloads are forwarded to this callback
 * rather than interpreted inside the network manager. Interpretation of
 * command semantics is left to the caller (orchestration layer).
 *
 * @param cb  Callback to invoke, or NULL to clear
 */
void network_manager_set_command_cb(transport_cmd_cb_t *cb);

#ifdef __cplusplus
}
#endif