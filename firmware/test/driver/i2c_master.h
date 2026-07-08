/**
 * @file i2c_master.h
 * @brief Minimal stand-in for ESP-IDF driver/i2c_master.h, for native
 * (host) SIL builds only.
 *
 * Only the types referenced by i2c_bus.h are defined. Both handle types
 * are opaque pointers here, matching their real ESP-IDF definition
 * closely enough that no driver code needs to know the difference;
 * mock_i2c_bus.c never dereferences them.
 *
 * Also provides stddef.h (for size_t): i2c_bus.h uses size_t in its
 * transfer function signatures without including stddef.h itself,
 * relying on some other real ESP-IDF header to have pulled it in
 * transitively. This is the first include inside i2c_bus.h, so it's
 * the natural place to cover that gap for the native build.
 */

#pragma once

#include <stddef.h>

typedef void *i2c_master_bus_handle_t;
typedef void *i2c_master_dev_handle_t;

typedef enum {
    I2C_NUM_0 = 0,
    I2C_NUM_1 = 1,
} i2c_port_t;