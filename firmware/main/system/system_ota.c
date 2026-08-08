// SPDX-License-Identifier: Apache-2.0

/**
 * @file system_ota.c
 * @brief Over-the-Air Firmware Update Implementation
 */

#include "system/system_ota.h"
#include "esp_app_desc.h"
#include "esp_http_client.h"
#include "esp_https_ota.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include <string.h>

#include "common/app_types.h"

// Display command queue, owned and defined by the display task module.
extern QueueHandle_t g_queue_display;

static const char *TAG = "system_ota";

#define OTA_DEFAULT_TIMEOUT_MS 5000
#define OTA_LOOP_DELAY_MS 20

// Local certificate for the OTA server (PEM format, null-terminated string)
static const char *local_server_cert =
    "-----BEGIN CERTIFICATE-----\n"
    "MIIDEzCCAfugAwIBAgIUVh+vSJzglCSV7n07nxzlwFXrPqgwDQYJKoZIhvcNAQEL\n"
    "BQAwGTEXMBUGA1UEAwwOMTkyLjE2OC4xOC4yMDcwHhcNMjYwNjA4MTM1MjEyWhcN\n"
    "MjcwNjA4MTM1MjEyWjAZMRcwFQYDVQQDDA4xOTIuMTY4LjE4LjIwNzCCASIwDQYJ\n"
    "KoZIhvcNAQEBBQADggEPADCCAQoCggEBAMr8GezPaTSzX4+70q1xfzpwwaJdUD4H\n"
    "dyzQ2Mdq5xqnJG1GIcMJwRCpSH0YSZCHKo+sdIp3zxMNisauQozgc57YgvhMnPOf\n"
    "0x2kFAxJo63LvBLn1haMjcDlO3otkYxelYQ1FeoHtLJzXll8T6STNK6ayBWLfHfA\n"
    "mNpYjz17ZMhhdJhrrHI6aZtAv6xqazyEJLgvTAK6w9rVT/0OaY/+JaTnQQaM6M6l\n"
    "uqpRoAvMwitsgmhtO0IUX6VGFJnuiiPpL2vL17eTuHMy4J7T0saTW4IZQRqRyAr2\n"
    "C0dP5PlBR+6d3k3qXmVqXi/m6bKfAN5n0oSOJ3V0cw5VxmerEtDmspsCAwEAAaNT\n"
    "MFEwHQYDVR0OBBYEFGxOuUeaZNTkhGwGWKsYNYloe12RMB8GA1UdIwQYMBaAFGxO\n"
    "uUeaZNTkhGwGWKsYNYloe12RMA8GA1UdEwEB/wQFMAMBAf8wDQYJKoZIhvcNAQEL\n"
    "BQADggEBAJyZKaL7+VpxGOkifrPDgW7+RksMKRZfZcRrAvSIt3QN+YQrwelNNkcg\n"
    "d3tlojMxI/OkUgrFkBk1rBSM/JRyWz48BBScxSAxTeb2XffZ5l3pmM4GFZ2/GzkC\n"
    "W0vVZ1je9oLCFuQzKgdZ1o/q/S9WjGUSnbSsvxdpBkOLDLzAlW1/wvqQvL8AvvVf\n"
    "1aqvVklw8XDRrCEJGy7g84Dpm57To0iE2Nt3cO2mcLV9zS/9hFd94YiNpQiiV7IN\n"
    "vmCbcsge2CJRsSMLRDPAnGiTQ5DBUI83HRSUSIemHG6P6QdKjAOIc7odazJPX3DE\n"
    "pCR3hW07mrFx1hJP/RG2nyGOT66hg8U=\n"
    "-----END CERTIFICATE-----\n";

/* ---------------------------- PRIVATE HELPER -------------------------- */
/**
 * @brief Push a status line to the display queue, if the display is present.
 *
 * No-op if g_queue_display has not been created (display absent or not
 * yet initialized). The send is non-blocking so an OTA in progress is
 * never stalled by the display task.
 *
 * @param text  Status text to show; truncated to fit the display message field
 * @param row   Target display row
 *
 * @return void
 */
static void update_display_status(const char *text, uint8_t row) {
    if (g_queue_display == NULL)
        return;

    display_msg_t msg;
    msg.row = row;
    strncpy(msg.text, text, sizeof(msg.text) - 1);
    msg.text[sizeof(msg.text) - 1] = '\0';

    xQueueSend(g_queue_display, &msg, 0); // Non-blocking
}

/* ---------------------------- UPDATE OPERATION -------------------------- */
esp_err_t system_ota_perform(const system_ota_config_t *cfg) {
    if (cfg == NULL || cfg->url == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "Starting stepped OTA update");

    // 1. Begin the HTTPS OTA session
    esp_http_client_config_t http_cfg = {
        .url                         = cfg->url,
        .timeout_ms                  = cfg->timeout_ms ? cfg->timeout_ms : OTA_DEFAULT_TIMEOUT_MS,
        .keep_alive_enable           = true,
        .skip_cert_common_name_check = cfg->skip_cert_check,
        .cert_pem = local_server_cert, // Using the local certificate for server verification
    };

    esp_https_ota_config_t https_ota_cfg = {.http_config = &http_cfg};
    esp_https_ota_handle_t ota_handle    = NULL;

    esp_err_t loop_err = esp_https_ota_begin(&https_ota_cfg, &ota_handle);
    if (loop_err != ESP_OK || ota_handle == NULL) {
        ESP_LOGE(TAG, "Failed to start HTTPS OTA session: %s", esp_err_to_name(loop_err));
        update_display_status("OTA: Init Failed", 2);
        return loop_err;
    }

    // 2. Stepped download loop
    int last_progress = -1;
    update_display_status("OTA: Connecting", 2);

    while (1) {
        loop_err = esp_https_ota_perform(ota_handle);

        if (loop_err != ESP_ERR_HTTPS_OTA_IN_PROGRESS) {
            break;
        }

        // Download still in progress; report progress to the display
        int read_bytes  = esp_https_ota_get_image_len_read(ota_handle);
        int total_bytes = esp_https_ota_get_image_size(ota_handle);

        if (total_bytes > 0) {
            int progress = (read_bytes * 100) / total_bytes;
            if (progress != last_progress) {
                last_progress = progress;

                char progress_str[17];
                snprintf(progress_str, sizeof(progress_str), "OTA: Down %d%%", progress);
                update_display_status(progress_str, 2);
            }
        }

        // Yield so a long download cannot starve other tasks
        vTaskDelay(pdMS_TO_TICKS(OTA_LOOP_DELAY_MS));
    }

    // 3. Evaluate the download result
    if (loop_err != ESP_OK) {
        ESP_LOGE(TAG, "OTA download interrupted: %s", esp_err_to_name(loop_err));
        update_display_status("OTA: Conn Lost", 2);
        esp_https_ota_finish(ota_handle); // Release the handle and any remaining connection state
        return loop_err;
    }

    // All chunks downloaded successfully; verify the new image's integrity (checksum/signature)
    update_display_status("OTA: Verifying", 2);
    esp_err_t finish_err = esp_https_ota_finish(ota_handle);

    if (finish_err != ESP_OK) {
        ESP_LOGE(TAG, "Firmware image checksum/signature validation failed: %s",
                 esp_err_to_name(finish_err));
        update_display_status("OTA: Bad Image", 2);
        return finish_err;
    }

    ESP_LOGI(TAG, "OTA update successful, new firmware is valid");
    update_display_status("OTA: Success!", 2);

    if (cfg->reboot_on_success) {
        ESP_LOGW(TAG, "Rebooting in 2 seconds...");
        vTaskDelay(pdMS_TO_TICKS(2000));
        esp_restart();
    }

    return ESP_OK;
}

/* ---------------------------- ROLLBACK MANAGEMENT -------------------------- */
bool system_ota_pending_verify(void) {
    const esp_partition_t *running = esp_ota_get_running_partition();
    if (running == NULL)
        return false;

    esp_ota_img_states_t state;
    if (esp_ota_get_state_partition(running, &state) != ESP_OK)
        return false;

    return state == ESP_OTA_IMG_PENDING_VERIFY;
}

esp_err_t system_ota_mark_valid(void) {
    esp_err_t ret = esp_ota_mark_app_valid_cancel_rollback();
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Running image confirmed valid; rollback cancelled");
    } else {
        ESP_LOGE(TAG, "Failed to mark image valid: %s", esp_err_to_name(ret));
    }
    return ret;
}

/* ---------------------------- DIAGNOSTIC -------------------------- */
esp_err_t system_ota_get_running_version(char *buf, size_t len) {
    if (buf == NULL || len == 0)
        return ESP_ERR_INVALID_ARG;

    const esp_partition_t *running = esp_ota_get_running_partition();
    if (running == NULL)
        return ESP_FAIL;

    esp_app_desc_t app_desc;
    if (esp_ota_get_partition_description(running, &app_desc) != ESP_OK)
        return ESP_FAIL;

    snprintf(buf, len, "%s", app_desc.version);
    return ESP_OK;
}