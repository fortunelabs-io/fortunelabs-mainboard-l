// SPDX-License-Identifier: Apache-2.0

/**
 * @file system_log.h
 * @brief Logging Facade Over ESP-IDF esp_log With an Optional Remote Sink
 *
 * This module deliberately does not introduce a parallel logging macro
 * system; application code keeps using ESP_LOGE / ESP_LOGW / ESP_LOGI /
 * ESP_LOGD as usual. It adds two capabilities on top of esp_log: runtime
 * level control per tag, and an optional remote sink that receives a
 * copy of every formatted line via a queue and forwarder task, so
 * logging never blocks on the sink's transport.
 */

#pragma once

#include "esp_err.h"
#include "esp_log.h"
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* -------------------------- FORWARDER BUFFER SIZING --------------------------*/
#define SYSTEM_LOG_LINE_MAX 160   ///< Per-line buffer size in bytes
#define SYSTEM_LOG_QUEUE_DEPTH 16 ///< Queued lines before drop-on-full

/* --------------------------- LOG SINK CALLBACK ----------------------------*/
/**
 * @brief Callback invoked for each formatted log line, for forwarding to a remote transport.
 *
 * @param line  Null-terminated formatted log line, including the ESP-IDF level/tag/timestamp prefix
 * @param len   Length of line in bytes, excluding the terminating null
 *
 * @return void
 */
typedef void (*system_log_sink_fn)(const char *line, size_t len);

/* --------------------------- LIFECYCLE ----------------------------*/
/**
 * @brief Initialize logging: set the global level and install the sink forwarder.
 *
 * Installs the queue, forwarder task, and vprintf hook needed for
 * system_log_register_sink() to work. Serial output continues
 * unaffected regardless of whether a sink is later registered.
 *
 * @param default_level  Global minimum level applied to all tags ("*")
 *
 * @return
 * - ESP_OK         : Logging facade ready
 * - ESP_ERR_NO_MEM : Queue or forwarder task allocation failed
 */
esp_err_t system_log_init(esp_log_level_t default_level);

/* --------------------------- LEVEL CONTROL ----------------------------*/
/**
 * @brief Set the minimum log level for a specific tag at runtime.
 *
 * Thin wrapper over esp_log_level_set().
 *
 * @param tag    Log tag to adjust, or "*" for the global level
 * @param level  Minimum level to emit for that tag
 *
 * @return void
 */
void system_log_set_level(const char *tag, esp_log_level_t level);

/* --------------------------- SINK MANAGEMENT ----------------------------*/
/**
 * @brief Register (or replace) the remote log sink and start forwarding.
 *
 * @param sink  Callback receiving each formatted line. NULL is equivalent
 *              to calling system_log_unregister_sink()
 *
 * @return
 * - ESP_OK                : Sink registered, forwarding active
 * - ESP_ERR_INVALID_STATE : system_log_init() was not called first
 */
esp_err_t system_log_register_sink(system_log_sink_fn sink);

/**
 * @brief Stop forwarding to the remote sink. Serial output is unaffected.
 *
 * @return void
 */
void system_log_unregister_sink(void);

#ifdef __cplusplus
}
#endif