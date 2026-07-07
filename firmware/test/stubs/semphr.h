/**
 * @file semphr.h
 * @brief Minimal stand-in for ESP-IDF freertos/semphr.h, for native (host)
 * SIL builds only.
 *
 * SemaphoreHandle_t is opaque here, matching its real FreeRTOS definition
 * (typedef struct QueueDefinition *SemaphoreHandle_t) closely enough that
 * no driver code needs to know the difference; mock_i2c_bus.c never
 * dereferences it.
 */

#pragma once

typedef void *SemaphoreHandle_t;