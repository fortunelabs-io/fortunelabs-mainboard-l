/**
 * @file FreeRTOS.h
 * @brief Minimal stand-in for ESP-IDF freertos/FreeRTOS.h, for native (host)
 * SIL builds only.
 *
 * The real FreeRTOS.h sets up kernel configuration consumed by other
 * FreeRTOS headers. Nothing in this test environment needs that
 * configuration.
 *
 * Forwards freertos/task.h defensively: ads1115.c calls vTaskDelay() and
 * pdMS_TO_TICKS() without including freertos/task.h itself. This may also
 * affect the real ESP32 build depending on what else pulls FreeRTOS
 * headers in transitively there; not something this stub fixes, just
 * routed around so the native test build isn't blocked by it. Worth
 * checking on the real target if this hasn't already been noticed.
 */

#pragma once

#include "freertos/task.h"