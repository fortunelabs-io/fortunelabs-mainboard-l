/**
 * @file task.h
 * @brief Minimal stand-in for ESP-IDF freertos/task.h, for native (host)
 * SIL builds only.
 *
 * pdMS_TO_TICKS is identity here (1 tick == 1 ms), which is close enough
 * for test purposes since no test asserts on the real FreeRTOS tick rate,
 * only on the millisecond figure the driver itself computed.
 *
 * vTaskDelay() is declared here but implemented in mock_i2c_bus.c as a
 * tracked no-op: it records the requested tick count instead of sleeping,
 * so tests can assert the requested duration without slowing the suite.
 * See mock_get_last_vtaskdelay_ticks() / mock_get_vtaskdelay_call_count().
 */

#pragma once

#include <stdint.h>

typedef uint32_t TickType_t;

#define pdMS_TO_TICKS(ms) ((TickType_t)(ms))

void vTaskDelay(TickType_t xTicksToDelay);