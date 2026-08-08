// SPDX-License-Identifier: Apache-2.0

/**
 * @file network_manager.c
 * @brief WiFi Station and MQTT Network Manager Implementation
 */

#include "network/network_manager.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "mqtt_client.h"
#include "system/system_config.h"
#include <string.h>

static const char *TAG = "network_manager";

// External dependency: live sensor value, owned by the sensor task.
extern float task_sensor_get_latest_voltage(void);

// Inbound-command callback, registered by the transport adapter. When set,
// command payloads are forwarded here rather than interpreted locally.
static transport_cmd_cb_t *s_cmd_cb = NULL;

/* --------------------------- MODULE STATE ----------------------------*/
static system_config_t          s_network_cfg;
static esp_mqtt_client_handle_t s_mqtt_client    = NULL;
static SemaphoreHandle_t        s_status_mutex   = NULL;
static bool                     s_wifi_connected = false;
static bool                     s_mqtt_connected = false;

// Topic strings, built once in init() from cfg->device_id
static char s_topic_telemetry[64];
static char s_topic_command[64];
static char s_topic_health[64];

/* ---------------------------- EVENT HANDLERS -------------------------- */
/**
 * @brief Handle WiFi station and IP-acquired events.
 *
 * Registered against both WIFI_EVENT and IP_EVENT_STA_GOT_IP (see init()),
 * so it dispatches on event_base internally rather than being split into
 * two handlers. On WIFI_EVENT_STA_START it connects; on
 * WIFI_EVENT_STA_DISCONNECTED it clears both WiFi and MQTT connected
 * state and reconnects; on IP_EVENT_STA_GOT_IP it marks WiFi connected
 * and starts the MQTT client.
 *
 * @param arg         Unused, required by esp_event_handler_instance_register() signature
 * @param event_base  WIFI_EVENT or IP_EVENT
 * @param event_id    Specific event within event_base
 * @param event_data  Event payload; cast to ip_event_got_ip_t for IP_EVENT_STA_GOT_IP
 *
 * @return void
 */
static void wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id,
                               void *event_data) {
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        ESP_LOGI(TAG, "WiFi started, connecting to AP...");
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        xSemaphoreTake(s_status_mutex, portMAX_DELAY);
        s_wifi_connected = false;
        s_mqtt_connected = false; // MQTT is also considered disconnected when WiFi is down
        xSemaphoreGive(s_status_mutex);
        ESP_LOGW(TAG, "WiFi disconnected, attempting to reconnect...");
        esp_wifi_connect();
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "Successfully got IP: " IPSTR, IP2STR(&event->ip_info.ip));

        xSemaphoreTake(s_status_mutex, portMAX_DELAY);
        s_wifi_connected = true;
        xSemaphoreGive(s_status_mutex);

        // Start the MQTT client once an IP address is acquired
        if (s_mqtt_client) {
            esp_mqtt_client_start(s_mqtt_client);
        }
    }
}

/**
 * @brief Handle MQTT client connection and inbound message events.
 *
 * On MQTT_EVENT_CONNECTED, marks MQTT connected and subscribes to the
 * command topic. On MQTT_EVENT_DISCONNECTED, marks MQTT disconnected.
 * On MQTT_EVENT_DATA, parses the payload for "ON"/"OFF" and forwards the
 * resulting state to the actuator queue.
 *
 * @param arg         Unused, required by esp_mqtt_client_register_event() signature
 * @param base        MQTT event base
 * @param event_id    Specific MQTT event (esp_mqtt_event_id_t)
 * @param event_data  Event payload, cast to esp_mqtt_event_handle_t
 *
 * @return void
 */
static void mqtt_event_handler(void *arg, esp_event_base_t base, int32_t event_id,
                               void *event_data) {
    esp_mqtt_event_handle_t event = event_data;
    switch ((esp_mqtt_event_id_t)event_id) {
        case MQTT_EVENT_CONNECTED: {
            ESP_LOGI(TAG, "MQTT client connected to broker: %s", s_network_cfg.broker_uri);
            xSemaphoreTake(s_status_mutex, portMAX_DELAY);
            s_mqtt_connected = true;
            xSemaphoreGive(s_status_mutex);

            // Subscribe to the command topic upon successful connection
            int msg_id = esp_mqtt_client_subscribe(s_mqtt_client, s_topic_command, 1);
            ESP_LOGI(TAG, "Subscribed to command topic, msg_id=%d", msg_id);
            break;
        }
        case MQTT_EVENT_DISCONNECTED:
            ESP_LOGW(TAG, "MQTT client disconnected from broker");
            xSemaphoreTake(s_status_mutex, portMAX_DELAY);
            s_mqtt_connected = false;
            xSemaphoreGive(s_status_mutex);
            break;
        case MQTT_EVENT_DATA:
            ESP_LOGI(TAG, "Inbound command received");
            ESP_LOGI(TAG, "Topic: %.*s", event->topic_len, event->topic);
            ESP_LOGI(TAG, "Payload: %.*s", event->data_len, event->data);

            // Forward the raw command to the registered handler; the
            // orchestration layer decides what the payload means.
            if (s_cmd_cb != NULL) {
                s_cmd_cb(event->topic, (const uint8_t *)event->data, (size_t)event->data_len);
            }
            break;
        default:
            break;
    }
}

/* ---------------------------- TASKS -------------------------- */
/**
 * @brief Periodic telemetry publish task.
 *
 * Every 5 seconds, if MQTT is connected, reads the latest sensor voltage
 * and publishes it as a JSON payload to the telemetry topic. Skips the
 * publish (without error) when MQTT is not yet connected.
 *
 * @param pvParameters  Unused, required by the FreeRTOS task signature
 *
 * @return void (never returns; runs for the lifetime of the task)
 */
static void task_network_telemetry(void *pvParameters) {
    (void)pvParameters;
    uint32_t tx_count = 0;
    char     payload[128]; // Buffer for JSON payload

    ESP_LOGI(TAG, "Telemetry task started, publishing every 5s");

    while (1) {
        bool ready = false; // false until MQTT connection is up
        xSemaphoreTake(s_status_mutex, portMAX_DELAY);
        ready = s_mqtt_connected;
        xSemaphoreGive(s_status_mutex);

        if (ready) {
            // Read the current sensor voltage from the sensor task
            float live_voltage = task_sensor_get_latest_voltage();

            // JSON payload shape: {"seq":"1","sensor_v":2.34,"status":"OK"}
            snprintf(payload, sizeof(payload),
                     "{\"seq\":\"%lu\",\"sensor_v\":%.2f,\"status\":\"OK\"}", tx_count++,
                     live_voltage);

            int msg_id =
                esp_mqtt_client_publish(s_mqtt_client, s_topic_telemetry, payload, 0, 1, 0);
            if (msg_id != -1) {
                ESP_LOGI(TAG, "Telemetry published successfully: (msg_id: %d) %s", msg_id, payload);
            } else {
                ESP_LOGE(TAG, "Failed to publish telemetry data");
            }
        } else {
            ESP_LOGD(TAG, "MQTT not ready, skipping telemetry publish...");
        }
        vTaskDelay(pdMS_TO_TICKS(5000)); // Publish every 5 seconds
    }
}

/* ---------------------------- LIFECYCLE ----------------------------*/
/**
 * @brief Initialize the network manager: TCP/IP stack, WiFi station, MQTT client.
 *
 * See network_manager.h for the full contract.
 *
 * @param cfg  Pointer to the system configuration structure
 *
 * @return
 * - ESP_OK              : Initialization completed successfully
 * - ESP_ERR_INVALID_ARG : cfg is NULL
 * - ESP_ERR_NO_MEM      : Status mutex allocation failed
 * - ESP_FAIL            : MQTT client construction failed
 */
esp_err_t network_manager_init(const system_config_t *cfg) {
    if (cfg == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    // Copy config into internal state
    memcpy(&s_network_cfg, cfg, sizeof(system_config_t));

    // Create mutex guarding s_wifi_connected / s_mqtt_connected
    s_status_mutex = xSemaphoreCreateMutex();
    if (s_status_mutex == NULL)
        return ESP_ERR_NO_MEM;

    // 1. Initialize TCP/IP stack and event loop
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    // 2. Initialize the WiFi driver, then apply station config from cfg.
    //    wifi_init_cfg (the driver init struct) and wifi_sta_config (the
    //    station config struct, declared below) are distinct types;
    //    named separately here to avoid shadowing wifi_config_t.
    wifi_init_config_t wifi_init_cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&wifi_init_cfg));

    // Register the WiFi/IP event handler
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                                        &wifi_event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                                        &wifi_event_handler, NULL, NULL));

    wifi_config_t wifi_sta_config = {0};
    strncpy((char *)wifi_sta_config.sta.ssid, s_network_cfg.wifi_ssid,
            sizeof(wifi_sta_config.sta.ssid));
    strncpy((char *)wifi_sta_config.sta.password, s_network_cfg.wifi_pass,
            sizeof(wifi_sta_config.sta.password));
    wifi_sta_config.sta.threshold.rssi  = -127; // Accept all RSSI levels
    wifi_sta_config.sta.pmf_cfg.capable = true; // Enable Protected Management Frames

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_sta_config));

    // Derive the telemetry/command/health topic strings from device_id
    snprintf(s_topic_telemetry, sizeof(s_topic_telemetry), "fortunelabs/device/%s/telemetry",
             s_network_cfg.device_id);
    snprintf(s_topic_command, sizeof(s_topic_command), "fortunelabs/device/%s/command",
             s_network_cfg.device_id);
    snprintf(s_topic_health, sizeof(s_topic_health), "fortunelabs/device/%s/health",
             s_network_cfg.device_id);
    ESP_LOGI(TAG, "Configured MQTT topics: Telemetry='%s', Command='%s', Health='%s'",
             s_topic_telemetry, s_topic_command, s_topic_health);

    // 3. Setup MQTT client config
    esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.uri            = s_network_cfg.broker_uri,
        .session.disable_clean_session = false,
        .network.reconnect_timeout_ms  = 5000, // Reconnect after 5s if connection is lost
        .session.keepalive             = 15,   // in seconds

        // LWT: notify the broker if the device goes offline unexpectedly
        .session.last_will = {
            .topic   = s_topic_health, // Publish to health topic
            .msg     = "{\"status\":\"OFFLINE_UNEXPECTED\"}",
            .msg_len = 0, // auto calculate length from null-terminated string
            .qos     = 1, // QoS 1 to ensure delivery of LWT message
            .retain = 1 // Retain 1 so that broker holds the last known status of the device for new
                        // subscribers
        }};

    s_mqtt_client = esp_mqtt_client_init(&mqtt_cfg);
    if (s_mqtt_client == NULL) {
        ESP_LOGE(TAG, "Failed to initialize MQTT client");
        return ESP_FAIL;
    }

    // Register MQTT event handler
    ESP_ERROR_CHECK(
        esp_mqtt_client_register_event(s_mqtt_client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL));
    ESP_LOGI(TAG, "Network manager initialized successfully. Target AP: '%s'",
             s_network_cfg.wifi_ssid);
    return ESP_OK;
}

/**
 * @brief Start the WiFi radio and the telemetry publish task.
 *
 * See network_manager.h for the full contract.
 *
 * @return
 * - ESP_OK         : WiFi started and telemetry task created
 * - ESP_ERR_NO_MEM : Telemetry task creation failed
 */
esp_err_t network_manager_start(void) {
    ESP_ERROR_CHECK(esp_wifi_start());

    BaseType_t ok =
        xTaskCreate(task_network_telemetry, "task_network_telemetry", 4096, NULL, 3, NULL);
    if (ok != pdPASS) {
        ESP_LOGE(TAG, "Failed to create telemetry task");
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

/* --------------------------- STATUS ----------------------------*/
/**
 * @brief Check whether the MQTT client is currently connected to the broker.
 *
 * @return true if connected, false otherwise (including before init)
 */
bool network_manager_is_connected(void) {
    bool connected = false;
    if (s_status_mutex != NULL) {
        xSemaphoreTake(s_status_mutex, portMAX_DELAY);
        connected = s_mqtt_connected;
        xSemaphoreGive(s_status_mutex);
    }
    return connected;
}

/* --------------------------- PUBLISH ----------------------------*/
/**
 * @brief Publish a pre-built JSON payload to the device's health topic.
 *
 * See network_manager.h for the full contract.
 *
 * @param json_payload  JSON string payload to publish
 * @param length        Length of json_payload in bytes
 *
 * @return
 * - ESP_OK                : Publish accepted by the MQTT client
 * - ESP_ERR_INVALID_STATE : MQTT client not initialized or not connected
 * - ESP_FAIL              : Underlying MQTT publish call failed
 */
esp_err_t network_manager_publish_health(const char *json_payload, size_t length) {
    if (s_mqtt_client == NULL || !s_mqtt_connected) {
        // Not yet connected; caller (e.g. system supervisor) falls back to local logging
        return ESP_ERR_INVALID_STATE;
    }

    // Publish health data using QoS 1 to ensure delivery to the broker
    int msg_id =
        esp_mqtt_client_publish(s_mqtt_client, s_topic_health, json_payload, (int)length, 1, 0);
    if (msg_id < 0) {
        ESP_LOGE(TAG, "Failed to publish health metrics to MQTT");
        return ESP_FAIL;
    }

    return ESP_OK;
}

/* --------------------------- TRANSPORT DELEGATES ----------------------------*/
/**
 * @brief Publish a payload to an arbitrary topic. See network_manager.h.
 */
esp_err_t network_manager_publish(const char *topic, const char *payload, size_t length) {
    if (s_mqtt_client == NULL || !s_mqtt_connected) {
        return ESP_ERR_INVALID_STATE;
    }

    int msg_id = esp_mqtt_client_publish(s_mqtt_client, topic, payload, (int)length, 1, 0);
    if (msg_id < 0) {
        ESP_LOGE(TAG, "Failed to publish to topic '%s'", topic);
        return ESP_FAIL;
    }
    return ESP_OK;
}

/**
 * @brief Subscribe to a topic on the broker. See network_manager.h.
 */
esp_err_t network_manager_subscribe(const char *topic) {
    if (s_mqtt_client == NULL || !s_mqtt_connected) {
        return ESP_ERR_INVALID_STATE;
    }

    int msg_id = esp_mqtt_client_subscribe(s_mqtt_client, topic, 1);
    if (msg_id < 0) {
        ESP_LOGE(TAG, "Failed to subscribe to topic '%s'", topic);
        return ESP_FAIL;
    }
    return ESP_OK;
}

/**
 * @brief Register the inbound-command callback. See network_manager.h.
 */
void network_manager_set_command_cb(transport_cmd_cb_t *cb) { s_cmd_cb = cb; }