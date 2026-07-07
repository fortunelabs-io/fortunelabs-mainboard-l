/**
 * @file FreeRTOS.h
 * @brief Empty stand-in for ESP-IDF freertos/FreeRTOS.h, for native (host)
 * SIL builds only.
 *
 * The real FreeRTOS.h sets up kernel configuration consumed by other
 * FreeRTOS headers. Nothing in this test environment needs that
 * configuration; this file exists only so #include "freertos/FreeRTOS.h"
 * resolves. freertos/semphr.h in this same stub set is self-contained and
 * does not depend on anything defined here.
 */

#pragma once