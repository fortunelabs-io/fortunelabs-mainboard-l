/**
 * @file test_mcp23017.c
 * @brief SIL (host-native) test suite for the MCP23017 driver.
 *
 * Exercises mcp23017.c unmodified against mock_i2c_bus. Covers register
 * write correctness, shadow RAM synchronization, the "zero I2C read on
 * RMW" claim, read-path integrity, error propagation, and guard-state
 * behavior. Electrical timing, real ACK behavior, and concurrency are
 * out of scope; see SIL_TEST_REPORT.md Section 5.
 */

#include "drivers/ic/mcp23017.h"
#include "mock_i2c_bus.h"
#include "unity.h"
#include <string.h>

/* --------------------------- FIXTURE STATE ---------------------------*/
static i2c_bus_t                s_fake_bus;
static mcp23017_t               s_dev;
static mcp23017_config_t        s_cfg;
static const mcp23017_driver_t *s_driver;

void setUp(void) {
    mock_i2c_bus_reset();
    memset(&s_fake_bus, 0, sizeof(s_fake_bus));
    memset(&s_dev, 0, sizeof(s_dev));
    memset(&s_cfg, 0, sizeof(s_cfg));
    s_driver = mcp23017_get_driver();

    s_cfg.bus      = &s_fake_bus;
    s_cfg.address  = MCP23017_ADDR_BASE;
    s_cfg.scl_hz   = 400000;
    s_cfg.dir_a    = 0x00; // default fixture: Port A all output
    s_cfg.dir_b    = 0xFF; // default fixture: Port B all input
    s_cfg.pullup_a = 0x00;
    s_cfg.pullup_b = 0xFF;
}

void tearDown(void) {
    // No resource release needed; setUp() reinitializes all fixture
    // state, including the mock bus, before every test.
}

/* --------------------------- INIT SEQUENCE ---------------------------*/

void test_init_writes_iodir_from_config(void) {
    TEST_ASSERT_EQUAL(ESP_OK, s_driver->init(&s_dev, &s_cfg));

    uint8_t iodira, iodirb;
    mock_i2c_bus_get_reg(MCP23017_REG_IODIRA, &iodira);
    mock_i2c_bus_get_reg(MCP23017_REG_IODIRB, &iodirb);

    TEST_ASSERT_EQUAL_HEX8(s_cfg.dir_a, iodira);
    TEST_ASSERT_EQUAL_HEX8(s_cfg.dir_b, iodirb);
}

void test_init_writes_pullup_from_config(void) {
    TEST_ASSERT_EQUAL(ESP_OK, s_driver->init(&s_dev, &s_cfg));

    uint8_t gppua, gppub;
    mock_i2c_bus_get_reg(MCP23017_REG_GPPUA, &gppua);
    mock_i2c_bus_get_reg(MCP23017_REG_GPPUB, &gppub);

    TEST_ASSERT_EQUAL_HEX8(s_cfg.pullup_a, gppua);
    TEST_ASSERT_EQUAL_HEX8(s_cfg.pullup_b, gppub);
}

void test_init_sets_shadow_to_config_values(void) {
    TEST_ASSERT_EQUAL(ESP_OK, s_driver->init(&s_dev, &s_cfg));

    TEST_ASSERT_EQUAL_HEX8(s_cfg.dir_a, s_dev.dir_a);
    TEST_ASSERT_EQUAL_HEX8(s_cfg.dir_b, s_dev.dir_b);
    TEST_ASSERT_EQUAL_HEX8(MCP23017_DEFAULT_OLAT, s_dev.olat_a);
    TEST_ASSERT_EQUAL_HEX8(MCP23017_DEFAULT_OLAT, s_dev.olat_b);
    TEST_ASSERT_TRUE(s_dev.is_initialized);
}

void test_init_writes_exactly_eight_registers_in_order(void) {
    // Locks down the init write sequence so a future reorder is caught
    // here rather than discovered on hardware.
    TEST_ASSERT_EQUAL(ESP_OK, s_driver->init(&s_dev, &s_cfg));
    TEST_ASSERT_EQUAL_UINT32(8, mock_i2c_bus_get_log_count());

    const uint8_t expected_order[8] = {
        MCP23017_REG_IOCON_BANK1, MCP23017_REG_IOCON, MCP23017_REG_IODIRA, MCP23017_REG_IODIRB,
        MCP23017_REG_GPPUA,       MCP23017_REG_GPPUB, MCP23017_REG_OLATA,  MCP23017_REG_OLATB,
    };

    for (uint32_t i = 0; i < 8; i++) {
        const mock_i2c_record_t *log = mock_i2c_bus_get_log_entry(i);
        TEST_ASSERT_NOT_NULL(log);
        TEST_ASSERT_EQUAL_HEX8(expected_order[i], log->reg_addr);
    }
}

void test_init_rejects_null_dev(void) {
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, s_driver->init(NULL, &s_cfg));
    TEST_ASSERT_EQUAL_UINT32(0, mock_i2c_bus_get_log_count());
}

void test_init_rejects_null_config(void) {
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, s_driver->init(&s_dev, NULL));
    TEST_ASSERT_EQUAL_UINT32(0, mock_i2c_bus_get_log_count());
}

void test_init_rejects_address_out_of_range(void) {
    s_cfg.address = MCP23017_ADDR_MAX + 1;
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, s_driver->init(&s_dev, &s_cfg));
    TEST_ASSERT_EQUAL_UINT32(0, mock_i2c_bus_get_log_count());
}

/* --------------------------- GUARD STATE ---------------------------*/

void test_uninitialized_write_pin_returns_invalid_state(void) {
    // s_dev is zero-initialized in setUp(); init() was never called.
    esp_err_t ret = s_driver->write_pin(&s_dev, MCP23017_PORT_A, 3, true);
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, ret);
    TEST_ASSERT_EQUAL_UINT32(0, mock_i2c_bus_get_log_count());
}

void test_uninitialized_read_pin_returns_invalid_state(void) {
    bool      value = false;
    esp_err_t ret   = s_driver->read_pin(&s_dev, MCP23017_PORT_A, 3, &value);
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, ret);
    TEST_ASSERT_EQUAL_UINT32(0, mock_i2c_bus_get_log_count());
}

/* --------------------------- WRITE_PIN ---------------------------*/

void test_write_pin_sets_shadow_bit(void) {
    TEST_ASSERT_EQUAL(ESP_OK, s_driver->init(&s_dev, &s_cfg)); // dir_a = 0x00, all output
    mock_i2c_bus_reset(); // isolate from init()'s own 8 log entries; s_dev survives

    esp_err_t ret = s_driver->write_pin(&s_dev, MCP23017_PORT_A, 3, true);

    TEST_ASSERT_EQUAL(ESP_OK, ret);
    TEST_ASSERT_EQUAL_HEX8(0x08, s_dev.olat_a);
}

void test_write_pin_clears_shadow_bit(void) {
    TEST_ASSERT_EQUAL(ESP_OK, s_driver->init(&s_dev, &s_cfg));
    TEST_ASSERT_EQUAL(ESP_OK, s_driver->write_pin(&s_dev, MCP23017_PORT_A, 3, true));
    TEST_ASSERT_EQUAL_HEX8(0x08, s_dev.olat_a); // fixture sanity check

    esp_err_t ret = s_driver->write_pin(&s_dev, MCP23017_PORT_A, 3, false);

    TEST_ASSERT_EQUAL(ESP_OK, ret);
    TEST_ASSERT_EQUAL_HEX8(0x00, s_dev.olat_a);
}

void test_write_pin_preserves_other_bits(void) {
    // A write to one pin must not disturb sibling bits already latched,
    // proving the RMW targets the shadow rather than overwriting the byte.
    TEST_ASSERT_EQUAL(ESP_OK, s_driver->init(&s_dev, &s_cfg));
    TEST_ASSERT_EQUAL(ESP_OK, s_driver->write_pin(&s_dev, MCP23017_PORT_A, 0, true));

    esp_err_t ret = s_driver->write_pin(&s_dev, MCP23017_PORT_A, 3, true);

    TEST_ASSERT_EQUAL(ESP_OK, ret);
    TEST_ASSERT_EQUAL_HEX8(0x09, s_dev.olat_a); // bit 0 and bit 3 both set
}

void test_write_pin_issues_single_i2c_write(void) {
    TEST_ASSERT_EQUAL(ESP_OK, s_driver->init(&s_dev, &s_cfg));
    mock_i2c_bus_reset();

    TEST_ASSERT_EQUAL(ESP_OK, s_driver->write_pin(&s_dev, MCP23017_PORT_A, 3, true));

    TEST_ASSERT_EQUAL_UINT32(1, mock_i2c_bus_get_log_count());
    const mock_i2c_record_t *log = mock_i2c_bus_get_log_entry(0);
    TEST_ASSERT_EQUAL(MOCK_I2C_OP_WRITE, log->op);
    TEST_ASSERT_EQUAL_HEX8(MCP23017_REG_OLATA, log->reg_addr);
    TEST_ASSERT_EQUAL_HEX8(0x08, log->value);
}

void test_write_pin_rejects_pin_out_of_range(void) {
    TEST_ASSERT_EQUAL(ESP_OK, s_driver->init(&s_dev, &s_cfg));
    mock_i2c_bus_reset();

    esp_err_t ret = s_driver->write_pin(&s_dev, MCP23017_PORT_A, MCP23017_PIN_MAX + 1, true);

    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, ret);
    TEST_ASSERT_EQUAL_UINT32(0, mock_i2c_bus_get_log_count());
}

/* --------------------------- TOGGLE_PIN ---------------------------*/

void test_toggle_pin_xors_shadow(void) {
    TEST_ASSERT_EQUAL(ESP_OK, s_driver->init(&s_dev, &s_cfg)); // olat_a starts 0x00

    TEST_ASSERT_EQUAL(ESP_OK, s_driver->toggle_pin(&s_dev, MCP23017_PORT_A, 2));
    TEST_ASSERT_EQUAL_HEX8(0x04, s_dev.olat_a);

    TEST_ASSERT_EQUAL(ESP_OK, s_driver->toggle_pin(&s_dev, MCP23017_PORT_A, 2));
    TEST_ASSERT_EQUAL_HEX8(0x00, s_dev.olat_a);
}

/* --------------------------- READ_PIN ---------------------------*/

void test_read_pin_reflects_live_gpio_not_shadow(void) {
    s_cfg.dir_a = 0xFF; // all input, so GPIOA reflects the external pin state
    TEST_ASSERT_EQUAL(ESP_OK, s_driver->init(&s_dev, &s_cfg));

    // Simulate pin 3 being driven HIGH externally
    mock_i2c_bus_set_reg(MCP23017_REG_GPIOA, 0x08);

    bool      value = false;
    esp_err_t ret   = s_driver->read_pin(&s_dev, MCP23017_PORT_A, 3, &value);

    TEST_ASSERT_EQUAL(ESP_OK, ret);
    TEST_ASSERT_TRUE(value);
}

void test_read_pin_does_not_touch_shadow(void) {
    s_cfg.dir_a = 0xFF;
    TEST_ASSERT_EQUAL(ESP_OK, s_driver->init(&s_dev, &s_cfg));
    uint8_t olat_before = s_dev.olat_a;

    mock_i2c_bus_set_reg(MCP23017_REG_GPIOA, 0xFF);
    bool value = false;
    TEST_ASSERT_EQUAL(ESP_OK, s_driver->read_pin(&s_dev, MCP23017_PORT_A, 0, &value));

    TEST_ASSERT_EQUAL_HEX8(olat_before, s_dev.olat_a); // read_pin must never mutate OLAT shadow
}

void test_read_pin_propagates_i2c_failure(void) {
    TEST_ASSERT_EQUAL(ESP_OK, s_driver->init(&s_dev, &s_cfg));
    mock_i2c_bus_force_next_result(ESP_FAIL);

    bool      value = true; // sentinel: must remain untouched if the call fails
    esp_err_t ret   = s_driver->read_pin(&s_dev, MCP23017_PORT_A, 3, &value);

    TEST_ASSERT_EQUAL(ESP_FAIL, ret);
    TEST_ASSERT_TRUE(value); // untouched, per early-return on failure in mcp23017_read_pin
}

void test_read_pin_rejects_null_output(void) {
    TEST_ASSERT_EQUAL(ESP_OK, s_driver->init(&s_dev, &s_cfg));
    mock_i2c_bus_reset();

    esp_err_t ret = s_driver->read_pin(&s_dev, MCP23017_PORT_A, 3, NULL);

    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, ret);
    TEST_ASSERT_EQUAL_UINT32(0, mock_i2c_bus_get_log_count());
}

/* --------------------------- PORT-LEVEL: DIRECTION / PULLUP ---------------------------*/

void test_set_pin_direction_updates_shadow_and_writes(void) {
    s_cfg.dir_a = 0xFF; // start all input
    TEST_ASSERT_EQUAL(ESP_OK, s_driver->init(&s_dev, &s_cfg));
    mock_i2c_bus_reset();

    esp_err_t ret = s_driver->set_pin_direction(&s_dev, MCP23017_PORT_A, 3, MCP23017_DIR_OUTPUT);

    TEST_ASSERT_EQUAL(ESP_OK, ret);
    TEST_ASSERT_EQUAL_HEX8(0xF7, s_dev.dir_a); // bit 3 cleared -> output, rest still input
    TEST_ASSERT_EQUAL_UINT32(1, mock_i2c_bus_get_log_count());
}

void test_set_port_direction_writes_directly_no_preceding_read(void) {
    TEST_ASSERT_EQUAL(ESP_OK, s_driver->init(&s_dev, &s_cfg));
    mock_i2c_bus_reset();

    esp_err_t ret = s_driver->set_port_direction(&s_dev, MCP23017_PORT_A, 0x0F);

    TEST_ASSERT_EQUAL(ESP_OK, ret);
    TEST_ASSERT_EQUAL_UINT32(1, mock_i2c_bus_get_log_count());
    const mock_i2c_record_t *log = mock_i2c_bus_get_log_entry(0);
    TEST_ASSERT_EQUAL(MOCK_I2C_OP_WRITE, log->op); // not WRITE_READ -> no read-before-write
    TEST_ASSERT_EQUAL_HEX8(MCP23017_REG_IODIRA, log->reg_addr);
    TEST_ASSERT_EQUAL_HEX8(0x0F, s_dev.dir_a);
}

void test_set_port_pullup_issues_full_write_every_call(void) {
    // GPPU has no shadow cache by design (see header note). Two identical
    // calls must each produce a real I2C write, not a short-circuited no-op.
    TEST_ASSERT_EQUAL(ESP_OK, s_driver->init(&s_dev, &s_cfg));
    mock_i2c_bus_reset();

    TEST_ASSERT_EQUAL(ESP_OK, s_driver->set_port_pullup(&s_dev, MCP23017_PORT_A, 0xFF));
    TEST_ASSERT_EQUAL(ESP_OK, s_driver->set_port_pullup(&s_dev, MCP23017_PORT_A, 0xFF));

    TEST_ASSERT_EQUAL_UINT32(2, mock_i2c_bus_get_log_count());
}

/* --------------------------- DEINIT ---------------------------*/

void test_deinit_issues_no_i2c_traffic(void) {
    TEST_ASSERT_EQUAL(ESP_OK, s_driver->init(&s_dev, &s_cfg));
    mock_i2c_bus_reset(); // clear init()'s own log entries; isolate deinit's behavior

    s_driver->deinit(&s_dev);

    TEST_ASSERT_EQUAL_UINT32(0, mock_i2c_bus_get_log_count());
}

void test_deinit_clears_context(void) {
    TEST_ASSERT_EQUAL(ESP_OK, s_driver->init(&s_dev, &s_cfg));

    s_driver->deinit(&s_dev);

    TEST_ASSERT_FALSE(s_dev.is_initialized);
    TEST_ASSERT_EQUAL_HEX8(0, s_dev.dir_a);
    TEST_ASSERT_EQUAL_HEX8(0, s_dev.olat_a);
}

void test_deinit_null_is_safe(void) {
    // Must not crash: deinit() checks dev == NULL and returns early.
    s_driver->deinit(NULL);
    TEST_PASS();
}

/* --------------------------- TEST RUNNER ---------------------------*/

int main(void) {
    UNITY_BEGIN();

    RUN_TEST(test_init_writes_iodir_from_config);
    RUN_TEST(test_init_writes_pullup_from_config);
    RUN_TEST(test_init_sets_shadow_to_config_values);
    RUN_TEST(test_init_writes_exactly_eight_registers_in_order);
    RUN_TEST(test_init_rejects_null_dev);
    RUN_TEST(test_init_rejects_null_config);
    RUN_TEST(test_init_rejects_address_out_of_range);

    RUN_TEST(test_uninitialized_write_pin_returns_invalid_state);
    RUN_TEST(test_uninitialized_read_pin_returns_invalid_state);

    RUN_TEST(test_write_pin_sets_shadow_bit);
    RUN_TEST(test_write_pin_clears_shadow_bit);
    RUN_TEST(test_write_pin_preserves_other_bits);
    RUN_TEST(test_write_pin_issues_single_i2c_write);
    RUN_TEST(test_write_pin_rejects_pin_out_of_range);

    RUN_TEST(test_toggle_pin_xors_shadow);

    RUN_TEST(test_read_pin_reflects_live_gpio_not_shadow);
    RUN_TEST(test_read_pin_does_not_touch_shadow);
    RUN_TEST(test_read_pin_propagates_i2c_failure);
    RUN_TEST(test_read_pin_rejects_null_output);

    RUN_TEST(test_set_pin_direction_updates_shadow_and_writes);
    RUN_TEST(test_set_port_direction_writes_directly_no_preceding_read);
    RUN_TEST(test_set_port_pullup_issues_full_write_every_call);

    RUN_TEST(test_deinit_issues_no_i2c_traffic);
    RUN_TEST(test_deinit_clears_context);
    RUN_TEST(test_deinit_null_is_safe);

    return UNITY_END();
}