// SPDX-License-Identifier: Apache-2.0
/**
 * @file main.c
 * @brief Fortune Labs Mainboard PoC application entry point.
 *
 * Sequence:
 *   1. NVS init
 *   2. System services (log, config, OTA check)
 *   3. I2C bus init + scan
 *   4. Hardware driver init (ADS1115, SSD1306, dummy sensor/actuator)
 *   5. Queue allocation
 *   6. Network manager init
 *   7. System supervisor init
 *   8. RTOS task spawn
 */

#include "esp_event.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include <string.h> // Added for string manipulation safety

/* --- HAL Contracts --- */
#include "bus/i2c_bus.h"
#include "common/app_types.h"
#include "hal/display_driver.h"
#include "hal/output_driver.h"
#include "hal/sensor_driver.h"

/* --- Task Contexts --- */
#include "tasks/task_actuator.h"
#include "tasks/task_display.h"
#include "tasks/task_sensor.h"

/* --- Concrete Drivers --- */
#include "drivers/ic/ads1115.h"
#include "drivers/ic/ssd1306.h"
#include "drivers/output/actuator_dummy.h"
#include "drivers/sensor/sensor_ads1115.h"
#include "drivers/transport/transport_mqtt.h"

/* --- System Services --- */
#include "system/system_config.h"
#include "system/system_log.h"
#include "system/system_ota.h"
#include "system/system_supervisor.h"

/* --- Network --- */
#include "network/network_manager.h"

// TODO: Comparison test between monolith OTA with stepped OTA

// I2c Bus Configuration
#define MAIN_I2C_SDA_PIN 18
#define MAIN_I2C_SCL_PIN 19
#define MAIN_I2C_PORT I2C_NUM_0

static const char *TAG      = "main";
static const char *TEST_TAG = "ota_trigger";

/* --- Global Resources ---
 * Queues are extern-accessible by tasks.
 * Bus and device instances are static: owned by main, passed by pointer.
 */
QueueHandle_t    g_queue_display;
QueueHandle_t    g_queue_actuator;
QueueHandle_t    g_queue_comm;
static i2c_bus_t g_i2c_bus;

// Task context instances, passed via xTaskCreate's pvParameters. Static
// because the task reads through this pointer for its entire lifetime.
static task_sensor_ctx_t   g_task_sensor_ctx;
static task_actuator_ctx_t g_task_actuator_ctx;
static task_display_ctx_t  g_task_display_ctx;

// Task entry points and context structs come from their respective headers
// in tasks/ (task_sensor.h, task_actuator.h, task_display.h).

/**
 * @brief Temporary R&D task: triggers OTA update 10s after boot.
 * @note Remove before production deployment.
 */
void ota_test_task(void *pvParameters) {
    ESP_LOGI(TEST_TAG, "Starting OTA test task in 10 seconds");
    vTaskDelay(pdMS_TO_TICKS(10000)); // Delay to allow system to stabilize

    ESP_LOGI(TEST_TAG, "Waiting for network connection before starting OTA");

    system_ota_config_t ota_cfg = {
        .url               = "https://192.168.18.207:8443/firmware.bin",
        .timeout_ms        = 10000,
        .reboot_on_success = true,
        .skip_cert_check   = true,
    };

    // Excute OTA main function
    esp_err_t ret = system_ota_perform(&ota_cfg);

    // Evaluate OTA result
    if (ret != ESP_OK) {
        ESP_LOGE(TEST_TAG, "OTA test failed. Error code: %s", esp_err_to_name(ret));
    }

    // Cleanup and delete task after OTA attempt
    vTaskDelete(NULL);
}

/**
 * @brief Inbound transport command handler.
 *
 * Interprets the raw broker payload and forwards the resulting state to the
 * actuator task queue. Command interpretation lives here, in orchestration,
 * rather than inside the transport implementation.
 */
static void app_transport_command(const char *topic, const uint8_t *data, size_t data_len) {
    (void)topic;
    if (data_len == 2 && strncmp((const char *)data, "ON", 2) == 0) {
        bool state = true;
        xQueueSend(g_queue_actuator, &state, 0);
        ESP_LOGI(TAG, "Remote control action: turned actuator ON");
    } else if (data_len == 3 && strncmp((const char *)data, "OFF", 3) == 0) {
        bool state = false;
        xQueueSend(g_queue_actuator, &state, 0);
        ESP_LOGI(TAG, "Remote control action: turned actuator OFF");
    }
}

void app_main(void) {
    ESP_LOGI(TAG, "FortuneLabs Mainboard PoC - booting...");

    /* [1] NVS ---------------------------------------------------------------- */
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    /* [2] System Log --------------------------------------------------------- */
    ESP_ERROR_CHECK(system_log_init(ESP_LOG_INFO)); // Set default log level to INFO
    ESP_LOGI(TAG, "FortuneLabs Mainboard PoC - Booting ...");

    /* [3] System Config ------------------------------------------------------ */
    ESP_ERROR_CHECK(system_config_init());
    system_config_t sys_cfg;
    ESP_ERROR_CHECK(system_config_load(&sys_cfg)); // Load config from NVS to RAM

    /* [DEV ONLY] WiFi credential fallback: inject hardcoded SSID if NVS blank.
     * Remove or replace with provisioning flow before production.
     */
    if (strlen(sys_cfg.wifi_ssid) == 0) {
        ESP_LOGW(TAG,
                 "System configuration in NVS is blank. Injecting R&D temporary network profile.");
        strncpy(sys_cfg.wifi_ssid, "FIRDAUS", sizeof(sys_cfg.wifi_ssid) - 1);
        strncpy(sys_cfg.wifi_pass, "Bismillah", sizeof(sys_cfg.wifi_pass) - 1);
        strncpy(sys_cfg.broker_uri, "mqtt://broker.hivemq.com:1883",
                sizeof(sys_cfg.broker_uri) - 1);
    }

    /* [4] OTA Rollback Check ------------------------------------------------- */
    if (system_ota_pending_verify()) {
        ESP_LOGI(TAG, "New Firmware booted successfully. Marking OTA as verified.");
        system_ota_mark_valid();
    }

    //* [5] I2C Bus ------------------------------------------------------------ */
    // Init I2C Master
    i2c_bus_config_t bus_cfg = {
        .sda_pin = MAIN_I2C_SDA_PIN,
        .scl_pin = MAIN_I2C_SCL_PIN,
        .port    = MAIN_I2C_PORT,
        .clk_hz  = 100000 // 100 kHz Fast Mode
    };
    ESP_LOGI(TAG, "Initializing I2C Master Bus...");
    ESP_ERROR_CHECK(i2c_bus_init(&g_i2c_bus, &bus_cfg));

    ESP_LOGI(TAG, "Pre-flight I2C bus scan...");
    i2c_bus_scan(&g_i2c_bus);

    /* [6] Queue Allocation --------------------------------------------------- */
    g_queue_display =
        xQueueCreate(10, sizeof(display_msg_t)); // using struct for display messages (row + text)
    g_queue_actuator = xQueueCreate(5, sizeof(bool)); // using bool for simple ON/OFF control

    // Depth 1: this is a latest-value mailbox, not a stream. task_sensor
    // overwrites it every sample and telemetry peeks it every 5 s, so a
    // deeper queue would only let telemetry fall behind the board.
    g_queue_comm = xQueueCreate(1, sizeof(sensor_reading_t));

    if (g_queue_display == NULL || g_queue_actuator == NULL || g_queue_comm == NULL) {
        ESP_LOGE(TAG, "Queue creation failed!");
        esp_restart();
    }

    /* [7] Driver Init -------------------------------------------------------- */
    ESP_LOGI(TAG, "Initializing hardware drivers...");

    // Init Potentiometer Dummy
    static sensor_ads1115_config_t ads_sensor_cfg = {
        .channel_config =
            {
                [0] = {.channel   = ADS1115_CHANNEL_0,
                       .pga       = ADS1115_PGA_4_096V,
                       .data_rate = ADS1115_DR_128SPS},
                [1] = {.channel   = ADS1115_CHANNEL_1,
                       .pga       = ADS1115_PGA_2_048V,
                       .data_rate = ADS1115_DR_64SPS},
                [2] = {.channel   = ADS1115_CHANNEL_2,
                       .pga       = ADS1115_PGA_4_096V,
                       .data_rate = ADS1115_DR_128SPS},
                [3] = {.channel   = ADS1115_CHANNEL_3,
                       .pga       = ADS1115_PGA_2_048V,
                       .data_rate = ADS1115_DR_64SPS},
            },
    };
    sensor_config_t sensor_cfg = {
        .bus      = &g_i2c_bus,
        .i2c_addr = ADS1115_ADDR_GND, // 0x48
        .channel  = 0,                // read AIN0
        .extra    = &ads_sensor_cfg,
    };
    ESP_ERROR_CHECK(sensor_ads1115_driver.init(&sensor_cfg));
    g_task_sensor_ctx.driver = &sensor_ads1115_driver;

    // Init Onboard LED (Actuator)
    output_config_t actuator_cfg = {.bus = NULL, .num_channels = 1};
    ESP_ERROR_CHECK(actuator_dummy_driver.init(&actuator_cfg));

    // Wire the contract pointer task_actuator will consume. Swapping the
    // dummy LED for the MCP23017+ULN2803A relay bank (or any future
    // output) means changing this one line, with zero changes inside
    // task_actuator.c.
    g_task_actuator_ctx.driver = &actuator_dummy_driver;

    // Init Physical SSD1306 OLED via I2C Bus
    display_config_t display_cfg = {.bus      = &g_i2c_bus,
                                    .i2c_addr = 0x3C, // Alamat I2C standard SSD1306
                                    .width    = 128,
                                    .height   = 64};
    ESP_ERROR_CHECK(ssd1306_driver.init(&display_cfg));

    // Wire the display contract pointer task_display will consume. Swapping
    // the SSD1306 for any future display means changing this one line, with
    // zero changes inside task_display.c.
    g_task_display_ctx.driver = &ssd1306_driver;

    /* [8] Transport (WiFi + MQTT) -------------------------------------------- */
    ESP_LOGI(TAG, "Initializing transport (WiFi + MQTT)...");
    // Reach outbound comms through the transport HAL contract. Swapping WiFi/
    // MQTT for a future connectivity path (cellular, LoRaWAN) means changing
    // the driver instance wired here, not the callers.
    transport_mqtt_config_t transport_extra = {
        .wifi_ssid = sys_cfg.wifi_ssid,
        .wifi_pass = sys_cfg.wifi_pass,
    };
    transport_config_t transport_cfg = {
        .broker_uri = sys_cfg.broker_uri,
        .device_id  = sys_cfg.device_id,
        .cmd_cb     = app_transport_command,
        .extra      = &transport_extra,
    };
    ESP_ERROR_CHECK(transport_mqtt_driver.init(&transport_cfg));

    /* [9] System Supervisor -------------------------------------------------- */
    system_supervisor_config_t supervisor_cfg = {
        .wdt_timeout_ms      = 10000, // 10 second watchdog timeout
        .trigger_panic       = true,
        .heartbeat_period_ms = 5000,               // Publish a heartbeat every 5 seconds
        .publish = network_manager_publish_health, // Placeholder for health publish function (MQTT
                                                   // publish in real)
        .device_id = sys_cfg.device_id             // Using device ID from system
    };
    ESP_ERROR_CHECK(system_supervisor_init(&supervisor_cfg));

    /* [10] Task Spawn -------------------------------------------------------- */
    xTaskCreate(ota_test_task, "task_ota_test", 8192, NULL, 3, NULL);
    // Spawn supervisor task first
    xTaskCreate(task_supervisor, "task_sys_sup", 3072, NULL, 6,
                NULL); // Highest priority for system supervisor
    xTaskCreate(task_sensor, "task_sensor", 3072, &g_task_sensor_ctx, 5,
                NULL); // Sensor task has higher priority for responsive readings
    xTaskCreate(task_actuator, "task_actuator", 2048, &g_task_actuator_ctx, 5, NULL);
    xTaskCreate(task_display, "task_display", 3072, &g_task_display_ctx, 4, NULL);

    ESP_LOGI(TAG, "All tasks spawned successfully. System is running.");
}