/**
 * @file test_sensor_ads1115.c
 * @brief SIL (host-native) test suite for the ADS1115 sensor HAL adapter.
 *
 * The raw ads1115 driver is already covered by test_ads1115. This suite
 * targets only what the adapter adds on top of it: the raw-to-voltage
 * transformation. Specifically the signed interpretation of the 16-bit
 * code, the PGA-derived LSB scaling, and the active-channel resolution.
 *
 * The adapter is exercised end to end through the same mock_i2c_bus used
 * by the raw driver tests: adapter -> ads1115_get_driver()->read() ->
 * i2c_bus_* -> mock. A seeded conversion register therefore surfaces as a
 * voltage at the contract boundary.
 *
 * Voltages are asserted in integer microvolts to avoid a dependency on
 * Unity float support. Tolerances are wide enough for float rounding but
 * far tighter than any wrong-LSB or unsigned-cast regression would be.
 */

#include "drivers/sensor/sensor_ads1115.h"
#include "mock_i2c_bus.h"
#include "unity.h"
#include <string.h>

/* --------------------------- TIME STUB ---------------------------*/
/* The adapter's only time dependency. Kept here, not in the mock, so a
 * test can set the returned time and assert timestamp_ms deterministically. */
static int64_t s_fake_time_us = 0;
int64_t        esp_timer_get_time(void) { return s_fake_time_us; }

/* --------------------------- FIXTURE STATE ---------------------------*/
static i2c_bus_t               s_fake_bus;
static sensor_config_t         s_cfg;
static sensor_ads1115_config_t s_ext;
static const sensor_driver_t  *s_sensor;

static void configure_channel(uint8_t idx, ads1115_channel_t ch, ads1115_pga_t pga) {
    s_ext.channel_config[idx].channel   = ch;
    s_ext.channel_config[idx].pga       = pga;
    s_ext.channel_config[idx].data_rate = ADS1115_DR_128SPS;
}

void setUp(void) {
    mock_i2c_bus_reset();
    memset(&s_fake_bus, 0, sizeof(s_fake_bus));
    memset(&s_cfg, 0, sizeof(s_cfg));
    memset(&s_ext, 0, sizeof(s_ext));
    s_fake_time_us = 0;
    s_sensor       = &sensor_ads1115_driver;

    // Channel 0 at 4.096V FSR; the other channels at 2.048V so a
    // channel-resolution error would also change the scaling.
    configure_channel(0, ADS1115_CHANNEL_0, ADS1115_PGA_4_096V);
    configure_channel(1, ADS1115_CHANNEL_1, ADS1115_PGA_2_048V);
    configure_channel(2, ADS1115_CHANNEL_2, ADS1115_PGA_2_048V);
    configure_channel(3, ADS1115_CHANNEL_3, ADS1115_PGA_2_048V);

    s_cfg.bus      = &s_fake_bus;
    s_cfg.i2c_addr = ADS1115_ADDR_GND;
    s_cfg.channel  = 0;
    s_cfg.extra    = &s_ext;
}

void tearDown(void) {
    // Clear the adapter's file-static singleton state between tests.
    s_sensor->deinit();
}

/* --------------------------- HELPERS ---------------------------*/
static int read_microvolts(uint16_t code) {
    TEST_ASSERT_EQUAL(ESP_OK, s_sensor->init(&s_cfg));
    mock_i2c_bus_set_reg16(ADS1115_REG_CONVERSION, code);

    sensor_reading_t out;
    memset(&out, 0, sizeof(out));
    TEST_ASSERT_EQUAL(ESP_OK, s_sensor->read(&out));
    return (int)(out.value * 1e6f);
}

/* --------------------------- SCALING: HAPPY PATH ---------------------------*/

void test_read_full_scale_positive(void) {
    // ch0 @ 4.096V FSR, code 0x7FFF -> 32767 * 4.096/32768 V = 4.095875 V
    TEST_ASSERT_INT_WITHIN(60, 4095875, read_microvolts(0x7FFF));
}

void test_read_zero(void) { TEST_ASSERT_INT_WITHIN(10, 0, read_microvolts(0x0000)); }

void test_read_half_scale_alternate_pga(void) {
    // ch1 @ 2.048V FSR, code 0x4000 (16384) -> 16384 * 2.048/32768 = 1.024 V
    s_cfg.channel = 1;
    TEST_ASSERT_EQUAL(ESP_OK, s_sensor->init(&s_cfg));
    mock_i2c_bus_set_reg16(ADS1115_REG_CONVERSION, 0x4000);

    sensor_reading_t out;
    memset(&out, 0, sizeof(out));
    TEST_ASSERT_EQUAL(ESP_OK, s_sensor->read(&out));
    TEST_ASSERT_INT_WITHIN(60, 1024000, (int)(out.value * 1e6f));
}

/* --------------------------- SCALING: SIGNED-CODE GUARD ---------------------------*/

void test_read_small_negative_not_wrapped(void) {
    // 0xFFFF is code -1, not 65535. Correct handling gives a tiny NEGATIVE
    // voltage (~ -125 uV at 4.096V FSR). An unsigned-cast regression would
    // give ~ +8.19 V. This is the test that catches that regression.
    int uv = read_microvolts(0xFFFF);
    TEST_ASSERT_TRUE(uv < 0);
    TEST_ASSERT_INT_WITHIN(20, -125, uv);
}

void test_read_most_negative_code(void) {
    // 0x8000 is the most-negative code (-32768) -> -4.096 V at 4.096V FSR.
    TEST_ASSERT_INT_WITHIN(60, -4096000, read_microvolts(0x8000));
}

/* --------------------------- CHANNEL RESOLUTION ---------------------------*/

void test_read_resolves_active_channel(void) {
    // channel index 2 must map to ADS1115_CHANNEL_2 in the config word the
    // driver writes. An off-by-one in the adapter would read the wrong pin.
    s_cfg.channel = 2;
    TEST_ASSERT_EQUAL(ESP_OK, s_sensor->init(&s_cfg));

    sensor_reading_t out;
    memset(&out, 0, sizeof(out));
    TEST_ASSERT_EQUAL(ESP_OK, s_sensor->read(&out));

    uint16_t word = 0;
    mock_i2c_bus_get_reg16(ADS1115_REG_CONFIG, &word);
    TEST_ASSERT_EQUAL_HEX16(ADS1115_CHANNEL_2 & 0x07, (word >> 12) & 0x07); // MUX bits 14:12
    TEST_ASSERT_EQUAL_UINT8(2, out.channel);
}

/* --------------------------- READING STRUCT POPULATION ---------------------------*/

void test_read_populates_channel_and_timestamp(void) {
    s_fake_time_us = 5000000; // 5,000,000 us -> 5000 ms
    TEST_ASSERT_EQUAL(ESP_OK, s_sensor->init(&s_cfg));
    mock_i2c_bus_set_reg16(ADS1115_REG_CONVERSION, 0x1000);

    sensor_reading_t out;
    memset(&out, 0, sizeof(out));
    TEST_ASSERT_EQUAL(ESP_OK, s_sensor->read(&out));
    TEST_ASSERT_EQUAL_UINT8(0, out.channel);
    TEST_ASSERT_EQUAL_UINT32(5000, out.timestamp_ms);
}

/* --------------------------- FAILURE PROPAGATION ---------------------------*/

void test_read_propagates_failure_leaves_out_untouched(void) {
    TEST_ASSERT_EQUAL(ESP_OK, s_sensor->init(&s_cfg));
    mock_i2c_bus_reset();                     // isolate call counter from init()'s own transfer
    mock_i2c_bus_force_next_result(ESP_FAIL); // fail the config write inside read()

    sensor_reading_t out;
    out.value        = 123.0f; // sentinels: adapter writes out only on success
    out.channel      = 9;
    out.timestamp_ms = 77;

    TEST_ASSERT_EQUAL(ESP_FAIL, s_sensor->read(&out));
    TEST_ASSERT_EQUAL_UINT8(9, out.channel);
    TEST_ASSERT_EQUAL_UINT32(77, out.timestamp_ms);
}

/* --------------------------- STATE / ARGUMENT GUARDS ---------------------------*/

void test_read_rejects_uninitialized(void) {
    sensor_reading_t out;
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, s_sensor->read(&out));
}

void test_read_rejects_null_out(void) {
    TEST_ASSERT_EQUAL(ESP_OK, s_sensor->init(&s_cfg));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, s_sensor->read(NULL));
}

void test_init_rejects_null_config(void) {
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, s_sensor->init(NULL));
}

void test_init_rejects_null_bus(void) {
    s_cfg.bus = NULL;
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, s_sensor->init(&s_cfg));
}

void test_init_rejects_null_extra(void) {
    s_cfg.extra = NULL;
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, s_sensor->init(&s_cfg));
}

void test_init_rejects_channel_out_of_range(void) {
    s_cfg.channel = 4; // valid range is 0..3
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, s_sensor->init(&s_cfg));
}

/* --------------------------- TEST RUNNER ---------------------------*/

int main(void) {
    UNITY_BEGIN();

    RUN_TEST(test_read_full_scale_positive);
    RUN_TEST(test_read_zero);
    RUN_TEST(test_read_half_scale_alternate_pga);

    RUN_TEST(test_read_small_negative_not_wrapped);
    RUN_TEST(test_read_most_negative_code);

    RUN_TEST(test_read_resolves_active_channel);
    RUN_TEST(test_read_populates_channel_and_timestamp);

    RUN_TEST(test_read_propagates_failure_leaves_out_untouched);

    RUN_TEST(test_read_rejects_uninitialized);
    RUN_TEST(test_read_rejects_null_out);
    RUN_TEST(test_init_rejects_null_config);
    RUN_TEST(test_init_rejects_null_bus);
    RUN_TEST(test_init_rejects_null_extra);
    RUN_TEST(test_init_rejects_channel_out_of_range);

    return UNITY_END();
}