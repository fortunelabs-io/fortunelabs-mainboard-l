// SPDX-License-Identifier: Apache-2.0
/**
 * @file transport_mqtt.c
 * @brief MQTT-over-WiFi Transport HAL Adapter Implementation
 *
 * Thin adapter that binds the network_manager (WiFi station + MQTT client)
 * to the transport_driver_t contract. The concrete WiFi/MQTT lifecycle,
 * event handling, and telemetry task remain owned by network_manager; this
 * adapter only maps the generic contract calls onto that implementation.
 */

#include "drivers/transport/transport_mqtt.h"
#include "esp_log.h"
#include "network/network_manager.h"
#include "system/system_config.h"
#include <string.h>

static const char *TAG = "transport_mqtt";

/* --------------------------- CONTRACT METHODS ----------------------------*/
/**
 * @brief Bring up WiFi + MQTT and register the inbound-command callback.
 *
 * @param cfg  Generic transport config. broker_uri, device_id, and cmd_cb
 *             are read directly; extra must point to a transport_mqtt_config_t
 *             carrying the WiFi credentials.
 *
 * @return
 * - ESP_OK              : Transport initialized and started
 * - ESP_ERR_INVALID_ARG : cfg, broker_uri, device_id, or extra is NULL
 * - Specific esp_err_t  : Underlying network_manager init/start failure
 */
static esp_err_t transport_mqtt_init(const transport_config_t *cfg) {
    if (cfg == NULL || cfg->broker_uri == NULL || cfg->device_id == NULL || cfg->extra == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    const transport_mqtt_config_t *ext = (const transport_mqtt_config_t *)cfg->extra;

    // Assemble the system_config snapshot the network manager consumes.
    // network_manager copies this internally, so the local is safe to drop.
    system_config_t sys_cfg;
    memset(&sys_cfg, 0, sizeof(sys_cfg));
    strlcpy(sys_cfg.wifi_ssid, ext->wifi_ssid ? ext->wifi_ssid : "", sizeof(sys_cfg.wifi_ssid));
    strlcpy(sys_cfg.wifi_pass, ext->wifi_pass ? ext->wifi_pass : "", sizeof(sys_cfg.wifi_pass));
    strlcpy(sys_cfg.broker_uri, cfg->broker_uri, sizeof(sys_cfg.broker_uri));
    strlcpy(sys_cfg.device_id, cfg->device_id, sizeof(sys_cfg.device_id));

    // Route inbound broker commands to the caller-provided callback.
    network_manager_set_command_cb(cfg->cmd_cb);

    esp_err_t err = network_manager_init(&sys_cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "network_manager_init failed: %s", esp_err_to_name(err));
        return err;
    }
    return network_manager_start();
}

/**
 * @brief Publish a payload to a topic through the MQTT client.
 */
static esp_err_t transport_mqtt_publish(const char *topic, const char *payload, size_t len) {
    return network_manager_publish(topic, payload, len);
}

/**
 * @brief Subscribe to a command/control topic through the MQTT client.
 */
static esp_err_t transport_mqtt_subscribe(const char *topic) {
    return network_manager_subscribe(topic);
}

/**
 * @brief Report whether the MQTT client is currently connected.
 */
static bool transport_mqtt_is_connected(void) { return network_manager_is_connected(); }

/**
 * @brief Release transport resources.
 *
 * The network stack has no teardown path today and lives for the process
 * lifetime, so this is a no-op placeholder for contract completeness.
 */
static void transport_mqtt_deinit(void) {
    ESP_LOGI(TAG, "transport deinit (no-op; network stack is process-lifetime)");
}

/* --------------------------- VTABLE REGISTRATION ----------------------------*/
const transport_driver_t transport_mqtt_driver = {
    .name         = "MQTT_WIFI",
    .init         = transport_mqtt_init,
    .publish      = transport_mqtt_publish,
    .subscribe    = transport_mqtt_subscribe,
    .is_connected = transport_mqtt_is_connected,
    .deinit       = transport_mqtt_deinit,
};
