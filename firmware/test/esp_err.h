/**
 * @file esp_err.h
 * @brief Minimal stand-in for the ESP-IDF esp_err.h, for native (host) SIL
 * builds only.
 *
 * Values match real ESP-IDF numbering so log output and error codes stay
 * meaningful if ever compared against a real device log. This file is
 * never reachable from the ESP32 target build; it is only found via the
 * env:native include path in platformio.ini.
 */

#pragma once

typedef int esp_err_t;

#define ESP_OK 0
#define ESP_FAIL -1

#define ESP_ERR_NO_MEM 0x101
#define ESP_ERR_INVALID_ARG 0x102
#define ESP_ERR_INVALID_STATE 0x103
#define ESP_ERR_INVALID_SIZE 0x104
#define ESP_ERR_NOT_FOUND 0x105
#define ESP_ERR_NOT_SUPPORTED 0x106
#define ESP_ERR_TIMEOUT 0x107

/**
 * @brief Minimal stand-in for esp_err_to_name(). Used by ads1115.c inside
 * ESP_LOGE/ESP_LOGW calls; string content is never asserted on by any
 * test, only kept human-readable for console output when a test fails.
 *
 * @param code  Error code to look up
 *
 * @return Statically-allocated, human-readable name for code
 */
static inline const char *esp_err_to_name(esp_err_t code) {
    switch (code) {
        case ESP_OK:
            return "ESP_OK";
        case ESP_FAIL:
            return "ESP_FAIL";
        case ESP_ERR_NO_MEM:
            return "ESP_ERR_NO_MEM";
        case ESP_ERR_INVALID_ARG:
            return "ESP_ERR_INVALID_ARG";
        case ESP_ERR_INVALID_STATE:
            return "ESP_ERR_INVALID_STATE";
        case ESP_ERR_INVALID_SIZE:
            return "ESP_ERR_INVALID_SIZE";
        case ESP_ERR_NOT_FOUND:
            return "ESP_ERR_NOT_FOUND";
        case ESP_ERR_NOT_SUPPORTED:
            return "ESP_ERR_NOT_SUPPORTED";
        case ESP_ERR_TIMEOUT:
            return "ESP_ERR_TIMEOUT";
        default:
            return "ESP_ERR_UNKNOWN";
    }
}