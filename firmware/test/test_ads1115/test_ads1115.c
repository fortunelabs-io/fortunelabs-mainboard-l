/**
 * @file test_ads1115.c
 * @brief SIL (host-native) test suite for the ADS1115 driver.
 *
 * Exercises ads1115.c unmodified against mock_i2c_bus (16-bit path) and
 * the tracked vTaskDelay stand-in. Scope, per explicit instruction: happy
 * path plus error/non-happy paths, not exhaustive edge-case coverage.
 * Electrical timing, real ACK behavior, and concurrency are out of
 * scope; see SIL_TEST_REPORT.md Section 5.
 */

#include "drivers/ic/ads1115.h"
#include "mock_i2c_bus.h"
#include "unity.h"
#include <string.h>

/* --------------------------- FIXTURE STATE ---------------------------*/
static i2c_bus_t               s_fake_bus;
static ads1115_dev_t           s_dev;
static ads1115_config_t        s_cfg;
static const ads1115_driver_t *s_driver;

void setUp(void) {
    mock_i2c_bus_reset();
    memset(&s_fake_bus, 0, sizeof(s_fake_bus));
    memset(&s_dev, 0, sizeof(s_dev));
    memset(&s_cfg, 0, sizeof(s_cfg));
    s_driver = ads1115_get_driver();

    s_cfg.bus  = &s_fake_bus;
    s_cfg.addr = ADS1115_ADDR_GND;

    // Only channel 0's slot is populated; every test below reads channel 0.
    s_cfg.channel_config[0].channel   = ADS1115_CHANNEL_0;
    s_cfg.channel_config[0].pga       = ADS1115_PGA_0_256V; // exercises non-zero PGA bits
    s_cfg.channel_config[0].data_rate = ADS1115_DR_128SPS;
}

void tearDown(void) {
    // No resource release needed; setUp() reinitializes all fixture
    // state, including the mock bus, before every test.
}

/* --------------------------- INIT ---------------------------*/

void test_init_writes_reset_config(void) {
    TEST_ASSERT_EQUAL(ESP_OK, s_driver->init(&s_dev, &s_cfg));

    uint16_t word = 0;
    mock_i2c_bus_get_reg16(ADS1115_REG_CONFIG, &word);
    TEST_ASSERT_EQUAL_HEX16(ADS1115_CONFIG_RESET, word);
}

void test_init_sets_initialized_flag(void) {
    TEST_ASSERT_EQUAL(ESP_OK, s_driver->init(&s_dev, &s_cfg));
    TEST_ASSERT_TRUE(s_dev.initialized);
}

void test_init_rejects_null_dev(void) {
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, s_driver->init(NULL, &s_cfg));
    TEST_ASSERT_EQUAL_UINT32(0, mock_i2c_bus_get_log_count());
}

void test_init_rejects_null_config(void) {
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, s_driver->init(&s_dev, NULL));
    TEST_ASSERT_EQUAL_UINT32(0, mock_i2c_bus_get_log_count());
}

void test_init_rejects_null_bus_in_config(void) {
    s_cfg.bus = NULL;
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, s_driver->init(&s_dev, &s_cfg));
    TEST_ASSERT_EQUAL_UINT32(0, mock_i2c_bus_get_log_count());
}

/* --------------------------- READ: HAPPY PATH ---------------------------*/

void test_read_writes_correctly_packed_config_word(void) {
    TEST_ASSERT_EQUAL(ESP_OK, s_driver->init(&s_dev, &s_cfg));

    uint16_t raw = 0;
    TEST_ASSERT_EQUAL(ESP_OK, s_driver->read(&s_dev, ADS1115_CHANNEL_0, &raw));

    uint16_t word = 0;
    mock_i2c_bus_get_reg16(ADS1115_REG_CONFIG, &word);

    // Verified field-by-field against ads1115_build_config_word's own bit
    // layout, using the same field values supplied in the fixture rather
    // than one hardcoded magic word, so this test survives even if the
    // enums' underlying integer values change.
    TEST_ASSERT_TRUE(word & (1u << 15));                                    // OS: start conversion
    TEST_ASSERT_EQUAL_HEX16(ADS1115_CHANNEL_0 & 0x07, (word >> 12) & 0x07); // MUX
    TEST_ASSERT_EQUAL_HEX16(ADS1115_PGA_0_256V & 0x07, (word >> 9) & 0x07); // PGA
    TEST_ASSERT_TRUE(word & (1u << 8));                                     // MODE: single-shot
    TEST_ASSERT_EQUAL_HEX16(ADS1115_DR_128SPS & 0x07, (word >> 5) & 0x07);  // DR
    TEST_ASSERT_EQUAL_HEX16(0x03, word & 0x03);                             // COMP_QUE: disabled
}

void test_read_returns_conversion_value(void) {
    TEST_ASSERT_EQUAL(ESP_OK, s_driver->init(&s_dev, &s_cfg));
    mock_i2c_bus_set_reg16(ADS1115_REG_CONVERSION, 0x1234);

    uint16_t  raw = 0;
    esp_err_t ret = s_driver->read(&s_dev, ADS1115_CHANNEL_0, &raw);

    TEST_ASSERT_EQUAL(ESP_OK, ret);
    TEST_ASSERT_EQUAL_HEX16(0x1234, raw);
}

void test_read_requests_correct_delay_for_data_rate(void) {
    // s_conversion_to_ms[ADS1115_DR_128SPS] = 8, plus ADS1115_CONVERSION_MARGIN_MS.
    // If this assertion ever fails on the margin constant specifically, that
    // constant's value is the thing to re-check, not this test's logic.
    TEST_ASSERT_EQUAL(ESP_OK, s_driver->init(&s_dev, &s_cfg));

    uint16_t raw = 0;
    TEST_ASSERT_EQUAL(ESP_OK, s_driver->read(&s_dev, ADS1115_CHANNEL_0, &raw));

    TEST_ASSERT_EQUAL_UINT32(1, mock_get_vtaskdelay_call_count());
    TEST_ASSERT_EQUAL_UINT32(8 + ADS1115_CONVERSION_MARGIN_MS, mock_get_last_vtaskdelay_ticks());
}

/* --------------------------- READ: ERROR / NON-HAPPY PATH ---------------------------*/

void test_read_rejects_uninitialized(void) {
    // s_dev is zero-initialized in setUp(); init() was never called.
    uint16_t  raw = 0;
    esp_err_t ret = s_driver->read(&s_dev, ADS1115_CHANNEL_0, &raw);
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, ret);
    TEST_ASSERT_EQUAL_UINT32(0, mock_i2c_bus_get_log_count());
}

void test_read_rejects_null_dev(void) {
    uint16_t raw = 0;
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, s_driver->read(NULL, ADS1115_CHANNEL_0, &raw));
}

void test_read_rejects_null_out_raw(void) {
    TEST_ASSERT_EQUAL(ESP_OK, s_driver->init(&s_dev, &s_cfg));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, s_driver->read(&s_dev, ADS1115_CHANNEL_0, NULL));
}

void test_read_rejects_invalid_channel(void) {
    TEST_ASSERT_EQUAL(ESP_OK, s_driver->init(&s_dev, &s_cfg));
    mock_i2c_bus_reset(); // isolate from init()'s own log entry; dev state survives

    uint16_t  raw = 0;
    esp_err_t ret = s_driver->read(&s_dev, (ads1115_channel_t)(ADS1115_CHANNEL_3 + 1), &raw);

    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, ret);
    TEST_ASSERT_EQUAL_UINT32(0, mock_i2c_bus_get_log_count());
}

void test_read_propagates_config_write_failure(void) {
    TEST_ASSERT_EQUAL(ESP_OK, s_driver->init(&s_dev, &s_cfg));
    mock_i2c_bus_reset();                     // isolate call counter from init()'s own transfer
    mock_i2c_bus_force_next_result(ESP_FAIL); // fails the very next call: the config write

    uint16_t  raw = 0xBEEF; // sentinel: must remain untouched on early failure
    esp_err_t ret = s_driver->read(&s_dev, ADS1115_CHANNEL_0, &raw);

    TEST_ASSERT_EQUAL(ESP_FAIL, ret);
    TEST_ASSERT_EQUAL_HEX16(0xBEEF, raw);
    TEST_ASSERT_EQUAL_UINT32(0, mock_get_vtaskdelay_call_count()); // never reached the delay
}

void test_read_propagates_conversion_read_failure(void) {
    TEST_ASSERT_EQUAL(ESP_OK, s_driver->init(&s_dev, &s_cfg));
    mock_i2c_bus_reset(); // isolate call counter from init()'s own transfer
    // Call #1 (config write) succeeds; call #2 (conversion write_read) fails.
    mock_i2c_bus_force_result_on_call(2, ESP_FAIL);

    uint16_t  raw = 0xBEEF;
    esp_err_t ret = s_driver->read(&s_dev, ADS1115_CHANNEL_0, &raw);

    TEST_ASSERT_EQUAL(ESP_FAIL, ret);
    TEST_ASSERT_EQUAL_HEX16(0xBEEF, raw);                          // untouched
    TEST_ASSERT_EQUAL_UINT32(1, mock_get_vtaskdelay_call_count()); // delay WAS reached this time
}

/* --------------------------- RESET ---------------------------*/

void test_reset_writes_reset_config(void) {
    TEST_ASSERT_EQUAL(ESP_OK, s_driver->init(&s_dev, &s_cfg));
    mock_i2c_bus_set_reg16(ADS1115_REG_CONFIG, 0x0000); // dirty it first

    TEST_ASSERT_EQUAL(ESP_OK, s_driver->reset(&s_dev));

    uint16_t word = 0;
    mock_i2c_bus_get_reg16(ADS1115_REG_CONFIG, &word);
    TEST_ASSERT_EQUAL_HEX16(ADS1115_CONFIG_RESET, word);
}

void test_reset_rejects_null_dev(void) {
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, s_driver->reset(NULL));
}

void test_reset_rejects_uninitialized(void) {
    // s_dev zero-initialized in setUp(); init() never called.
    esp_err_t ret = s_driver->reset(&s_dev);
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, ret);
    TEST_ASSERT_EQUAL_UINT32(0, mock_i2c_bus_get_log_count());
}

/* --------------------------- DEINIT ---------------------------*/

void test_deinit_clears_initialized_flag(void) {
    TEST_ASSERT_EQUAL(ESP_OK, s_driver->init(&s_dev, &s_cfg));

    s_driver->deinit(&s_dev);

    TEST_ASSERT_FALSE(s_dev.initialized);
}

void test_deinit_does_not_clear_bus_pointer(void) {
    // Documents actual behavior: unlike mcp23017_deinit (full memset),
    // ads1115_deinit only clears the initialized flag; bus and
    // channel_config are left as they were.
    TEST_ASSERT_EQUAL(ESP_OK, s_driver->init(&s_dev, &s_cfg));

    s_driver->deinit(&s_dev);

    TEST_ASSERT_EQUAL_PTR(&s_fake_bus, s_dev.bus);
}

void test_deinit_null_is_safe(void) {
    // Must not crash: deinit() checks dev == NULL and returns early.
    s_driver->deinit(NULL);
    TEST_PASS();
}

/* --------------------------- TEST RUNNER ---------------------------*/

int main(void) {
    UNITY_BEGIN();

    RUN_TEST(test_init_writes_reset_config);
    RUN_TEST(test_init_sets_initialized_flag);
    RUN_TEST(test_init_rejects_null_dev);
    RUN_TEST(test_init_rejects_null_config);
    RUN_TEST(test_init_rejects_null_bus_in_config);

    RUN_TEST(test_read_writes_correctly_packed_config_word);
    RUN_TEST(test_read_returns_conversion_value);
    RUN_TEST(test_read_requests_correct_delay_for_data_rate);

    RUN_TEST(test_read_rejects_uninitialized);
    RUN_TEST(test_read_rejects_null_dev);
    RUN_TEST(test_read_rejects_null_out_raw);
    RUN_TEST(test_read_rejects_invalid_channel);
    RUN_TEST(test_read_propagates_config_write_failure);
    RUN_TEST(test_read_propagates_conversion_read_failure);

    RUN_TEST(test_reset_writes_reset_config);
    RUN_TEST(test_reset_rejects_null_dev);
    RUN_TEST(test_reset_rejects_uninitialized);

    RUN_TEST(test_deinit_clears_initialized_flag);
    RUN_TEST(test_deinit_does_not_clear_bus_pointer);
    RUN_TEST(test_deinit_null_is_safe);

    return UNITY_END();
}