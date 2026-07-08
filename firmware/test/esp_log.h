/**
 * @file esp_log.h
 * @brief Minimal stand-in for the ESP-IDF esp_log.h, for native (host) SIL
 * builds only.
 *
 * Routes ESP_LOGx calls to printf so driver-side error logs stay visible
 * next to Unity's PASS/FAIL output when a test fails. Log level filtering
 * (LOG_LOCAL_LEVEL) is not implemented; every level always prints.
 */

#pragma once

#include <stdio.h>

#define ESP_LOGE(tag, fmt, ...) fprintf(stderr, "E (%s) " fmt "\n", tag, ##__VA_ARGS__)
#define ESP_LOGW(tag, fmt, ...) fprintf(stderr, "W (%s) " fmt "\n", tag, ##__VA_ARGS__)
#define ESP_LOGI(tag, fmt, ...) fprintf(stdout, "I (%s) " fmt "\n", tag, ##__VA_ARGS__)
#define ESP_LOGD(tag, fmt, ...) fprintf(stdout, "D (%s) " fmt "\n", tag, ##__VA_ARGS__)
#define ESP_LOGV(tag, fmt, ...) fprintf(stdout, "V (%s) " fmt "\n", tag, ##__VA_ARGS__)