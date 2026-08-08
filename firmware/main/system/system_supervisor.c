// SPDX-License-Identifier: Apache-2.0

/**
 * @file system_supervisor.c
 * @brief System Health Supervisor Implementation
 */

#include "system/system_supervisor.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_task_wdt.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdio.h>
#include <string.h>

static const char *TAG = "system_supervisor";

// Stored copy of caller config
static struct {
    uint32_t              wdt_timeout_ms;
    uint32_t              heartbeat_period_ms;
    sys_health_publish_fn publish;
    char                  device_id[SYSTEM_SUP_DEVICE_ID_MAX];
} s_cfg;

static bool s_initialized = false;

/* ---------------------------- PRIVATE HELPER -------------------------- */
/**
 * @brief Build the heartbeat JSON payload from current uptime and heap stats.
 *
 * @param buf     Destination buffer for the null-terminated JSON payload
 * @param buf_sz  Capacity of buf in bytes
 *
 * @return Number of bytes written (excluding the null terminator), or -1 on error
 */
static int build_heartbeat(char *buf, size_t buf_sz) {
    uint64_t      uptime_s  = (uint64_t)(esp_timer_get_time() / 1000000);
    unsigned long free_heap = (unsigned long)esp_get_free_heap_size();
    unsigned long min_heap  = (unsigned long)esp_get_minimum_free_heap_size();

    int len = snprintf(buf, buf_sz,
                       "{\"device_id\":\"%s\",\"uptime_s\":%llu,"
                       "\"free_heap\":%lu,\"min_free_heap\":%lu}",
                       s_cfg.device_id, uptime_s, free_heap, min_heap);

    return (len > 0 && len < (int)buf_sz) ? len : -1;
}

/* ---------------------------- LIFECYCLE -------------------------- */
esp_err_t system_supervisor_init(const system_supervisor_config_t *cfg) {
    if (cfg == NULL || cfg->wdt_timeout_ms == 0)
        return ESP_ERR_INVALID_ARG;

    esp_task_wdt_config_t twdt_cfg = {
        .timeout_ms     = cfg->wdt_timeout_ms,
        .idle_core_mask = 0, // watch explicitly-registered tasks only
        .trigger_panic  = cfg->trigger_panic,
    };

    // The TWDT may already be running from ESP-IDF's own startup path
    esp_err_t ret = esp_task_wdt_init(&twdt_cfg);
    if (ret == ESP_ERR_INVALID_STATE)
        ret = esp_task_wdt_reconfigure(&twdt_cfg);

    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "TWDT init/reconfigure failed: %s", esp_err_to_name(ret));
        return ret;
    }

    // Cache config; duplicate the caller-owned device_id string
    s_cfg.wdt_timeout_ms      = cfg->wdt_timeout_ms;
    s_cfg.heartbeat_period_ms = cfg->heartbeat_period_ms;
    s_cfg.publish             = cfg->publish;
    strlcpy(s_cfg.device_id, cfg->device_id ? cfg->device_id : "unknown", sizeof(s_cfg.device_id));

    if (cfg->heartbeat_period_ms >= cfg->wdt_timeout_ms)
        ESP_LOGW(TAG,
                 "heartbeat_period (%lums) >= wdt_timeout (%lums); "
                 "supervisor self-feeds at a safe sub-cadence",
                 (unsigned long)cfg->heartbeat_period_ms, (unsigned long)cfg->wdt_timeout_ms);

    s_initialized = true;
    ESP_LOGI(TAG, "Supervisor ready (wdt=%lums panic=%d heartbeat=%lums)",
             (unsigned long)cfg->wdt_timeout_ms, cfg->trigger_panic,
             (unsigned long)cfg->heartbeat_period_ms);
    return ESP_OK;
}

/* ---------------------------- PER-TASK WATCHDOG MEMBERSHIP -------------------------- */
esp_err_t system_supervisor_task_register(void) {
    return esp_task_wdt_add(NULL); // NULL = calling task
}

esp_err_t system_supervisor_task_feed(void) { return esp_task_wdt_reset(); }

esp_err_t system_supervisor_task_unregister(void) { return esp_task_wdt_delete(NULL); }

/* ---------------------------- SUPERVISOR TASK -------------------------- */
void task_supervisor(void *pvParameters) {
    (void)pvParameters;

    esp_err_t ret = system_supervisor_task_register();
    if (ret != ESP_OK)
        ESP_LOGE(TAG, "Self watchdog register failed: %s", esp_err_to_name(ret));

    // Feed at most every wdt_timeout/2, and never less often than the
    // heartbeat period. This decouples feed cadence from publish cadence.
    uint32_t tick_ms = s_cfg.wdt_timeout_ms / 2;
    if (tick_ms == 0 || tick_ms > s_cfg.heartbeat_period_ms)
        tick_ms = s_cfg.heartbeat_period_ms;
    if (tick_ms == 0)
        tick_ms = 1000;

    char     json[SYSTEM_SUP_HEARTBEAT_MAX];
    uint32_t since_publish_ms = s_cfg.heartbeat_period_ms; // publish on first loop

    ESP_LOGI(TAG, "Heartbeat task started (feed every %lums)", (unsigned long)tick_ms);

    while (1) {
        system_supervisor_task_feed();
        since_publish_ms += tick_ms;

        if (since_publish_ms >= s_cfg.heartbeat_period_ms) {
            since_publish_ms = 0;

            int len = build_heartbeat(json, sizeof(json));
            if (len > 0) {
                ESP_LOGI(TAG, "%s", json);
                if (s_cfg.publish != NULL) {
                    esp_err_t pr = s_cfg.publish(json, (size_t)len);
                    if (pr != ESP_OK)
                        ESP_LOGW(TAG, "Heartbeat publish failed: %s", esp_err_to_name(pr));
                }
            }
        }
        vTaskDelay(pdMS_TO_TICKS(tick_ms));
    }
}