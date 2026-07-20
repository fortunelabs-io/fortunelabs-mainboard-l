/**
 * @file esp_timer.h
 * @brief Minimal esp_timer stub for SIL. Declares only what drivers use.
 *
 * The implementation of esp_timer_get_time() is supplied by the test
 * translation unit, so a test can control the returned time and assert
 * timestamp mapping deterministically.
 */
#pragma once
#include <stdint.h>
int64_t esp_timer_get_time(void);