// SPDX-License-Identifier: Apache-2.0

/**
 * @file system_supervisor.h
 * @brief System Health Supervisor: Task Watchdog + Heartbeat Telemetry
 *
 * Bundles three system-health concerns that naturally belong together:
 *
 * 1. Task Watchdog (TWDT): tasks subscribe via system_supervisor_task_register()
 *    and must feed periodically via system_supervisor_task_feed(); a stuck
 *    task is detected within the configured timeout.
 *
 * 2. Panic/recovery: on TWDT expiry the ESP-IDF panic handler runs
 *    (backtrace + reboot). This is delegated, not re-implemented, so the
 *    crash backtrace is preserved.
 *
 * 3. Heartbeat: the supervisor task (task_supervisor) periodically
 *    publishes uptime and heap stats as JSON to a remote sink, proving
 *    the scheduler is alive.
 *
 * Constraint: a watched task must call system_supervisor_task_feed() more
 * often than wdt_timeout_ms, otherwise it trips the watchdog. The
 * supervisor task feeds itself automatically at a safe sub-timeout
 * cadence, decoupled from the heartbeat publish cadence.
 */

#pragma once

#include "esp_err.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* -------------------------- INTERNAL SIZING --------------------------*/
#define SYSTEM_SUP_DEVICE_ID_MAX 32  ///< Max device_id length including null terminator
#define SYSTEM_SUP_HEARTBEAT_MAX 160 ///< Max heartbeat JSON payload length

/* --------------------------- HEARTBEAT PUBLISH CALLBACK ----------------------------*/
/**
 * @brief Callback invoked by the supervisor task to ship a heartbeat payload to a remote transport.
 *
 * @param json  Null-terminated JSON heartbeat payload
 * @param len   Length of json in bytes, excluding the terminating null
 *
 * @return
 * - ESP_OK             : Payload accepted by the transport
 * - Specific esp_err_t : Transport-specific publish failure
 */
typedef esp_err_t (*sys_health_publish_fn)(const char *json, size_t len);

/* --------------------------- CONFIG STRUCT ----------------------------*/
/**
 * @brief Config for the watchdog and heartbeat subsystem.
 *
 * @param wdt_timeout_ms       Watchdog timeout per subscribed task, in ms
 * @param trigger_panic        If true, a TWDT expiry triggers the ESP-IDF panic handler
 * @param heartbeat_period_ms  Interval between heartbeat publishes, in ms
 * @param publish              Sink for heartbeat payloads (NULL disables publishing)
 * @param device_id            Identifier embedded in each heartbeat payload
 */
typedef struct {
    uint32_t              wdt_timeout_ms;
    bool                  trigger_panic;
    uint32_t              heartbeat_period_ms;
    sys_health_publish_fn publish;
    const char           *device_id;
} system_supervisor_config_t;

/* --------------------------- LIFECYCLE ----------------------------*/
/**
 * @brief Initialize the task watchdog and store heartbeat parameters.
 *
 * @param cfg  Pointer to a populated system_supervisor_config_t
 *
 * @return
 * - ESP_OK              : Watchdog configured and heartbeat parameters stored
 * - ESP_ERR_INVALID_ARG : cfg is NULL or wdt_timeout_ms is zero
 * - Specific esp_err_t  : TWDT init/reconfigure failure
 */
esp_err_t system_supervisor_init(const system_supervisor_config_t *cfg);

/* --------------------------- PER-TASK WATCHDOG MEMBERSHIP ----------------------------*/
/**
 * @brief Subscribe the calling task to the watchdog.
 *
 * @return
 * - ESP_OK             : Calling task is now watched
 * - Specific esp_err_t : TWDT subscription failure (e.g. not initialized)
 */
esp_err_t system_supervisor_task_register(void);

/**
 * @brief Feed (reset) the watchdog for the calling task.
 *
 * @return
 * - ESP_OK             : Watchdog reset for the calling task
 * - Specific esp_err_t : Calling task is not subscribed
 */
esp_err_t system_supervisor_task_feed(void);

/**
 * @brief Unsubscribe the calling task from the watchdog.
 *
 * @return
 * - ESP_OK             : Calling task removed from monitoring
 * - Specific esp_err_t : Calling task was not subscribed
 */
esp_err_t system_supervisor_task_unregister(void);

/* --------------------------- SUPERVISOR TASK ----------------------------*/
/**
 * @brief Heartbeat task: self-feeds the watchdog and publishes health telemetry.
 *
 * @param pvParameters  Unused, required by the FreeRTOS task signature
 *
 * @return void (never returns; runs for the lifetime of the task)
 */
void task_supervisor(void *pvParameters);

#ifdef __cplusplus
}
#endif