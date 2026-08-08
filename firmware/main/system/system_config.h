// SPDX-License-Identifier: Apache-2.0

/**
 * @file system_config.h
 * @brief NVS-Backed Configuration Store
 *
 * Persists WiFi credentials, MQTT broker URI, and device ID across
 * reboots. Values resolve in this order: NVS (if previously set) falls
 * back to a Kconfig compile-time default, and device_id additionally
 * falls back to a MAC-derived identifier if never explicitly set.
 */

#pragma once

#include "esp_err.h"
#include "stddef.h"
#include "stdint.h"

#ifdef __cplusplus
extern "C" {
#endif

/* -------------------------- FIELD CAPACITIES --------------------------*/
#define SYSTEM_CFG_SSID_LEN 33 ///< Max SSID length including null terminator
#define SYSTEM_CFG_PASS_LEN 64 ///< Max WiFi passphrase length including null terminator
#define SYSTEM_CFG_URI_LEN 128 ///< Max broker URI length including null terminator
#define SYSTEM_CFG_ID_LEN 32   ///< Max device ID length including null terminator

/* --------------------------- CONFIG SNAPSHOT ----------------------------*/
/**
 * @brief Resolved configuration values handed to the rest of the system.
 *
 * @param wifi_ssid   Target access point SSID
 * @param wifi_pass   Passphrase for the target access point
 * @param broker_uri  MQTT broker endpoint
 * @param device_id   Stable per-device identifier used in MQTT topics
 */
typedef struct {
    char wifi_ssid[SYSTEM_CFG_SSID_LEN];
    char wifi_pass[SYSTEM_CFG_PASS_LEN];
    char broker_uri[SYSTEM_CFG_URI_LEN];
    char device_id[SYSTEM_CFG_ID_LEN];
} system_config_t;

/* --------------------------- LIFECYCLE ----------------------------*/
/**
 * @brief Open the configuration NVS namespace and prepare the store.
 *
 * @return
 * - ESP_OK   : Namespace opened and store ready
 * - ESP_FAIL / Specific esp_err_t : NVS not initialized, or nvs_open() failed
 */
esp_err_t system_config_init(void);

/**
 * @brief Resolve all configuration values into the caller's snapshot.
 *
 * @param out  Pointer to the caller-allocated snapshot to populate
 *
 * @return
 * - ESP_OK                : Snapshot fully populated
 * - ESP_ERR_INVALID_ARG   : out is NULL
 * - ESP_ERR_INVALID_STATE : system_config_init() was not called first
 * - Specific esp_err_t    : NVS read failure on a present key
 */
esp_err_t system_config_load(system_config_t *out);

/* --------------------------- RUNTIME PROVISIONING ----------------------------*/
/**
 * @brief Persist WiFi credentials to NVS, overriding the Kconfig default.
 *
 * @param ssid  Null-terminated SSID (at most SYSTEM_CFG_SSID_LEN - 1 chars)
 * @param pass  Null-terminated passphrase (at most SYSTEM_CFG_PASS_LEN - 1 chars)
 *
 * @return
 * - ESP_OK                : Both keys written and committed
 * - ESP_ERR_INVALID_ARG   : ssid is NULL or either value exceeds capacity
 * - ESP_ERR_INVALID_STATE : Store not initialized
 * - Specific esp_err_t    : NVS write/commit failure
 */
esp_err_t system_config_set_wifi(const char *ssid, const char *pass);

/**
 * @brief Persist the MQTT broker URI to NVS.
 *
 * @param uri  Null-terminated broker URI (at most SYSTEM_CFG_URI_LEN - 1 chars)
 *
 * @return
 * - ESP_OK                : Key written and committed
 * - ESP_ERR_INVALID_ARG   : uri is NULL or exceeds capacity
 * - ESP_ERR_INVALID_STATE : Store not initialized
 * - Specific esp_err_t    : NVS write/commit failure
 */
esp_err_t system_config_set_broker(const char *uri);

/**
 * @brief Copy the resolved device identifier into the caller's buffer.
 *
 * @param buf  Destination buffer for the null-terminated ID
 * @param len  Capacity of buf in bytes
 *
 * @return
 * - ESP_OK                : ID copied successfully
 * - ESP_ERR_INVALID_ARG   : buf is NULL or len is zero
 * - ESP_ERR_INVALID_STATE : Store not initialized
 */
esp_err_t system_config_get_device_id(char *buf, size_t len);

/**
 * @brief Erase all stored config, reverting to Kconfig defaults.
 *
 * @return
 * - ESP_OK                : Namespace erased and committed
 * - ESP_ERR_INVALID_STATE : Store not initialized
 * - Specific esp_err_t    : NVS erase/commit failure
 */
esp_err_t system_config_reset(void);

#ifdef __cplusplus
}
#endif