// SPDX-License-Identifier: Apache-2.0

/**
 * @file system_ota.h
 * @brief Over-the-Air Firmware Update With Rollback Protection
 *
 * Uses the stepped OTA pattern (esp_https_ota_begin -> perform -> finish)
 * rather than the monolithic blocking esp_https_ota() call, for two
 * reasons:
 *
 * 1. Real-time UI feedback: the stepped loop can be interrupted between
 *    esp_https_ota_perform() calls to read download progress
 *    (esp_https_ota_get_image_len_read() / _get_image_size()) and push
 *    it to the display, which the monolithic call cannot do.
 *
 * 2. FreeRTOS task starvation: network I/O inside the monolithic call
 *    is long and unpredictable, and can starve time-sensitive tasks
 *    (e.g. high-frequency sensor sampling) enough to trip a watchdog.
 *    The stepped loop yields via vTaskDelay() between chunks so the
 *    rest of the mainboard keeps running during a download.
 *
 * Rollback protection is provided by the ESP-IDF app rollback
 * mechanism: a freshly flashed image boots in PENDING_VERIFY state and
 * must be confirmed via system_ota_mark_valid() or the bootloader
 * rolls back to the previous image on the next reset.
 */

#pragma once

#include "esp_err.h"
#include "stdbool.h"
#include "stddef.h"
#include "stdint.h"

#ifdef __cplusplus
extern "C" {
#endif

/* --------------------------- CONFIG STRUCT ----------------------------*/
/**
 * @brief Parameters for a single OTA update attempt.
 *
 * @param url                Firmware image URL
 * @param cert_pem           Server certificate in PEM format for TLS
 * @param timeout_ms         Per-request network timeout in ms
 * @param reboot_on_success  If true, reboot into the new image immediately after verification
 * @param skip_cert_check    If true, skip TLS common-name verification
 */
typedef struct {
    const char *url;
    const char *cert_pem;
    uint32_t    timeout_ms;
    bool        reboot_on_success;
    bool        skip_cert_check;
} system_ota_config_t;

/* --------------------------- UPDATE OPERATION ----------------------------*/
/**
 * @brief Download, verify, and flash a firmware image.
 *
 * Blocks for the duration of the download. On success and if
 * cfg->reboot_on_success is true, reboots into the new image before
 * returning.
 *
 * @param cfg  Pointer to a populated system_ota_config_t
 *
 * @return
 * - ESP_OK              : Image flashed and boot partition set
 * - ESP_ERR_INVALID_ARG : cfg or cfg->url is NULL
 * - Specific esp_err_t  : Network, validation, or flash error; the running image is left untouched
 */
esp_err_t system_ota_perform(const system_ota_config_t *cfg);

/* --------------------------- ROLLBACK MANAGEMENT ----------------------------*/
/**
 * @brief Report whether the running image is awaiting rollback confirmation.
 *
 * @return
 * - true  : Running image is in PENDING_VERIFY state
 * - false : Image already confirmed, or rollback is not enabled
 */
bool system_ota_pending_verify(void);

/**
 * @brief Confirm the running image is healthy and cancel pending rollback.
 *
 * @return
 * - ESP_OK             : Image marked valid, rollback cancelled
 * - Specific esp_err_t : State update failure
 */
esp_err_t system_ota_mark_valid(void);

/* --------------------------- DIAGNOSTIC ----------------------------*/
/**
 * @brief Copy the running application's version string into a buffer.
 *
 * @param buf  Destination buffer for the null-terminated version
 * @param len  Capacity of buf in bytes (32 is sufficient)
 *
 * @return
 * - ESP_OK              : Version copied successfully
 * - ESP_ERR_INVALID_ARG : buf is NULL or len is zero
 * - ESP_FAIL            : No running partition, or its description could not be read
 */
esp_err_t system_ota_get_running_version(char *buf, size_t len);

#ifdef __cplusplus
}
#endif