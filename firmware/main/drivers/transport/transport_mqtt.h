// SPDX-License-Identifier: Apache-2.0

/**
 * @file transport_mqtt.h
 * @brief MQTT-over-WiFi Transport HAL Adapter
 *
 * Binds the WiFi + MQTT network manager to the generic transport_driver_t
 * contract, so the orchestration layer reaches outbound communication
 * through the same interface it would use for any future connectivity
 * method (cellular, LoRaWAN). Swapping the transport is a one-line change
 * in main.c, following the same substitution reasoning as the sensor and
 * output HAL contracts (architecture Section 6.2).
 *
 * WiFi credentials are transport-specific and are not part of the generic
 * transport_config_t, so they are passed through transport_config_t.extra
 * as a transport_mqtt_config_t.
 */

#pragma once

#include "hal/transport_driver.h"

#ifdef __cplusplus
extern "C" {
#endif

/* --------------------------- CONFIG STRUCT ----------------------------*/
/**
 * @brief MQTT-transport-specific init payload, passed via transport_config_t.extra.
 *
 * @param wifi_ssid  Target access point SSID
 * @param wifi_pass  Passphrase for the target access point
 */
typedef struct {
    const char *wifi_ssid;
    const char *wifi_pass;
} transport_mqtt_config_t;

/* --------------------------- DRIVER INSTANCE ----------------------------*/
/**
 * @brief Exported instance of the MQTT-over-WiFi transport adapter.
 */
extern const transport_driver_t transport_mqtt_driver;

#ifdef __cplusplus
}
#endif
