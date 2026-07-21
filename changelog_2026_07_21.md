# Changelog 2026-07-21: Architectural Refactor

Scope: the large architectural refactors from `EXIT_AUDIT_REPORT.md` Section 1b.
Verification: `idf.py build` (esp32) passes; `pio test -e native` passes all 59 test cases.
Behavior is preserved. Runtime networking paths (broker connect, publish, inbound
command) are compile-verified only, since the build environment has no broker.

Summary of changes:

1. ADS1115 driver: removed internal vtable, exposed plain public functions, clock now from config (Decision #1).
2. MCP23017 driver: removed internal vtable, exposed plain public functions (Decision #1).
3. task_display: now consumes an injected `display_driver_t` pointer instead of a concrete driver header (Decision #3).
4. network_manager: wrapped behind `transport_driver_t` via a new transport adapter (Section 6.2).
5. transport_driver.h: fixed `cmd_cb` member from function type to function pointer.
6. Command interpretation moved from network_manager into the orchestration layer.
7. Unit tests updated to call the de-vtabled drivers directly.

---

## Revision 2026-07-21 (later same day): sections 1 and 2 reverted

After review, the architect decided the de-vtabled IC drivers were harder to read
day to day, and chose to keep the internal vtable as the house style. Sections 1
and 2 below (ADS1115 and MCP23017 de-vtable) were therefore reverted. The
`ads1115_driver_t` + `ads1115_get_driver()` and `mcp23017_driver_t` +
`mcp23017_get_driver()` vtables are back, and the unit tests call through
`*_get_driver()` again. `gold_standard.md` Decision #1 was amended to sanction the
inner vtable, so code and constitution agree.

Two improvements from the reverted work were kept:

- ADS1115 clock now comes from `config->scl_hz` instead of a hardcoded value.
- Internal static helpers keep their leading-underscore names.

Sections 3, 4, and 5 (task_display dependency injection, transport_driver_t
adapter, command routing to orchestration) were not reverted and still stand.

The section 1 and 2 snippets below are retained as a record of the reverted work.
The "New" blocks in those two sections no longer reflect the current code, except
for the retained `scl_hz` change.

Verification after revert: `idf.py build` passes; `pio test -e native` passes all
59 test cases.

---

## 1. ADS1115: remove internal vtable, clock from config

### 1a. `main/drivers/ic/ads1115.h`

Old:

```c
/**
 * @brief ADS1115 driver interface (vtable).
 * ...obtained via ads1115_get_driver().
 */
typedef struct {
    esp_err_t (*init)(ads1115_dev_t *dev, const ads1115_config_t *cfg);
    esp_err_t (*read)(ads1115_dev_t *dev, ads1115_channel_t channel, uint16_t *out_raw);
    esp_err_t (*reset)(ads1115_dev_t *dev);
    void (*deinit)(ads1115_dev_t *dev);
} ads1115_driver_t;

// Concrete Driver Access
const ads1115_driver_t *ads1115_get_driver(void);
```

New:

```c
/* --------------------------- PUBLIC FUNCTIONS ----------------------------*/
/**
 * @brief ADS1115 driver operations (fixed-BOM IC pattern, no vtable).
 * Every function takes a caller-allocated ads1115_dev_t context as its
 * first argument. Callers reach the driver only through these functions.
 * ...
 */

/** @brief Initialize driver, register I2C device, apply per-channel config */
esp_err_t ads1115_init(ads1115_dev_t *dev, const ads1115_config_t *cfg);
/** @brief Trigger single-shot conversion and return raw result */
esp_err_t ads1115_read(ads1115_dev_t *dev, ads1115_channel_t channel, uint16_t *out_raw);
/** @brief Write factory default value to config register */
esp_err_t ads1115_reset(ads1115_dev_t *dev);
/** @brief De-init all resources held by driver instance */
void ads1115_deinit(ads1115_dev_t *dev);
```

Config struct gained a `scl_hz` field.

Old:

```c
typedef struct {
    i2c_bus_t               *bus;
    ads1115_addr_t           addr;
    ads1115_channel_config_t channel_config[4];
} ads1115_config_t;
```

New:

```c
typedef struct {
    i2c_bus_t               *bus;
    ads1115_addr_t           addr;
    uint32_t                 scl_hz;
    ads1115_channel_config_t channel_config[4];
} ads1115_config_t;
```

### 1b. `main/drivers/ic/ads1115.c`

Functions changed from `static` to public, hardcoded clock replaced with config value, vtable singleton and accessor removed.

Old:

```c
/* ---------------------------- vTable Implementation -------------------------- */
// 1. Init
static esp_err_t ads1115_init(ads1115_dev_t *dev, const ads1115_config_t *config) {
    ...
    esp_err_t ret =
        i2c_bus_add_device(config->bus, (uint8_t)config->addr, 100000, "ADS1115", &dev->dev);
    ...
}

// * vTable Singleton
static const ads1115_driver_t s_ads1115_driver = {
    .init   = ads1115_init,
    .read   = ads1115_read,
    .reset  = ads1115_reset,
    .deinit = ads1115_deinit,
};

const ads1115_driver_t *ads1115_get_driver(void) { return &s_ads1115_driver; }
```

New:

```c
/* ---------------------------- Public Functions -------------------------- */
// 1. Init
esp_err_t ads1115_init(ads1115_dev_t *dev, const ads1115_config_t *config) {
    ...
    esp_err_t ret =
        i2c_bus_add_device(config->bus, (uint8_t)config->addr, config->scl_hz, "ADS1115", &dev->dev);
    ...
}
```

(`ads1115_read`, `ads1115_reset`, `ads1115_deinit` likewise lost their `static`
qualifier. The vtable singleton and `ads1115_get_driver()` were deleted.)

### 1c. `main/drivers/sensor/sensor_ads1115.c`

The adapter now supplies the clock (config layer) and calls the driver functions directly.

Old:

```c
static const ads1115_driver_t *s_drv               = NULL;
static ads1115_dev_t           s_dev               = {0};
...
    s_drv                              = ads1115_get_driver();

    ads1115_config_t ads_cfg = {
        .bus  = cfg->bus,
        .addr = (ads1115_addr_t)cfg->i2c_addr,
    };
    ...
    esp_err_t err = s_drv->init(&s_dev, &ads_cfg);
...
    esp_err_t err = s_drv->read(&s_dev, s_active_channel, &raw);
...
    if (s_drv != NULL && s_initialized) {
        s_drv->deinit(&s_dev);
    }
```

New:

```c
#define SENSOR_ADS1115_SCL_HZ 100000

static ads1115_dev_t s_dev = {0};
...
    ads1115_config_t ads_cfg = {
        .bus    = cfg->bus,
        .addr   = (ads1115_addr_t)cfg->i2c_addr,
        .scl_hz = SENSOR_ADS1115_SCL_HZ,
    };
    ...
    esp_err_t err = ads1115_init(&s_dev, &ads_cfg);
...
    esp_err_t err = ads1115_read(&s_dev, s_active_channel, &raw);
...
    if (s_initialized) {
        ads1115_deinit(&s_dev);
    }
```

---

## 2. MCP23017: remove internal vtable

### 2a. `main/drivers/ic/mcp23017.h`

The 11-entry vtable typedef and `mcp23017_get_driver()` were replaced with plain
public function declarations.

Old:

```c
typedef struct {
    esp_err_t (*init)(mcp23017_t *dev, const mcp23017_config_t *cfg);
    void (*deinit)(mcp23017_t *dev);
    esp_err_t (*set_port_direction)(mcp23017_t *dev, mcp23017_port_t port, uint8_t dir_mask);
    esp_err_t (*write_port)(mcp23017_t *dev, mcp23017_port_t port, uint8_t value);
    esp_err_t (*read_port)(mcp23017_t *dev, mcp23017_port_t port, uint8_t *output_value);
    esp_err_t (*set_port_pullup)(mcp23017_t *dev, mcp23017_port_t port, uint8_t pullup_mask);
    esp_err_t (*set_port_polarity)(mcp23017_t *dev, mcp23017_port_t port, uint8_t polarity_mask);
    esp_err_t (*set_pin_direction)(mcp23017_t *dev, mcp23017_port_t port, uint8_t pin,
                                   mcp23017_direction_t direction);
    esp_err_t (*write_pin)(mcp23017_t *dev, mcp23017_port_t port, uint8_t pin, bool value);
    esp_err_t (*read_pin)(mcp23017_t *dev, mcp23017_port_t port, uint8_t pin, bool *out_value);
    esp_err_t (*toggle_pin)(mcp23017_t *dev, mcp23017_port_t port, uint8_t pin);
} mcp23017_driver_t;

const mcp23017_driver_t *mcp23017_get_driver(void);
```

New:

```c
/** @brief Initialize driver, register I2C device, apply direction/pullup/OLAT config */
esp_err_t mcp23017_init(mcp23017_t *dev, const mcp23017_config_t *cfg);
/** @brief Clear context struct; issues no I2C transactions, hardware state is left as-is */
void mcp23017_deinit(mcp23017_t *dev);
esp_err_t mcp23017_set_port_direction(mcp23017_t *dev, mcp23017_port_t port, uint8_t dir_mask);
esp_err_t mcp23017_write_port(mcp23017_t *dev, mcp23017_port_t port, uint8_t value);
esp_err_t mcp23017_read_port(mcp23017_t *dev, mcp23017_port_t port, uint8_t *output_value);
esp_err_t mcp23017_set_port_pullup(mcp23017_t *dev, mcp23017_port_t port, uint8_t pullup_mask);
esp_err_t mcp23017_set_port_polarity(mcp23017_t *dev, mcp23017_port_t port, uint8_t polarity_mask);
esp_err_t mcp23017_set_pin_direction(mcp23017_t *dev, mcp23017_port_t port, uint8_t pin,
                                     mcp23017_direction_t direction);
esp_err_t mcp23017_write_pin(mcp23017_t *dev, mcp23017_port_t port, uint8_t pin, bool value);
esp_err_t mcp23017_read_pin(mcp23017_t *dev, mcp23017_port_t port, uint8_t pin, bool *out_value);
esp_err_t mcp23017_toggle_pin(mcp23017_t *dev, mcp23017_port_t port, uint8_t pin);
```

### 2b. `main/drivers/ic/mcp23017.c`

Each of the 11 functions lost its `static` qualifier. The vtable singleton and
accessor were deleted.

Old:

```c
// * vTable Singleton
static const mcp23017_driver_t s_mcp23017_driver = {
    .init               = mcp23017_init,
    .deinit             = mcp23017_deinit,
    .set_port_direction = mcp23017_set_port_direction,
    .write_port         = mcp23017_write_port,
    .read_port          = mcp23017_read_port,
    .set_port_pullup    = mcp23017_set_port_pullup,
    .set_port_polarity  = mcp23017_set_port_polarity,
    .set_pin_direction  = mcp23017_set_pin_direction,
    .write_pin          = mcp23017_write_pin,
    .read_pin           = mcp23017_read_pin,
    .toggle_pin         = mcp23017_toggle_pin,
};

const mcp23017_driver_t *mcp23017_get_driver(void) { return &s_mcp23017_driver; }
```

New: removed entirely.

---

## 3. task_display: dependency injection

### 3a. New file `main/tasks/task_display.h`

```c
#pragma once
#include "hal/display_driver.h"
...
typedef struct {
    const display_driver_t *driver;
} task_display_ctx_t;

void task_display(void *pvParameters);
```

### 3b. `main/tasks/task_display.c`

Old:

```c
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "common/app_types.h"
#include "hal/display_driver.h"
#include "drivers/ic/ssd1306.h"
#include "esp_log.h"

void task_display(void *pvParameters)
{
    ESP_LOGI(TAG, "Display task started.");
    display_msg_t incoming_msg;

    while (1)
    {
        if (xQueueReceive(g_queue_display, &incoming_msg, portMAX_DELAY) == pdTRUE)
        {
            ssd1306_driver.show_text(incoming_msg.row, incoming_msg.text);
        }
    }
}
```

New:

```c
#include "tasks/task_display.h"
#include "common/app_types.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

void task_display(void *pvParameters)
{
    ESP_LOGI(TAG, "Display task started.");

    const task_display_ctx_t *ctx = (const task_display_ctx_t *)pvParameters;
    const display_driver_t   *drv = ctx->driver;

    display_msg_t incoming_msg;

    while (1)
    {
        if (xQueueReceive(g_queue_display, &incoming_msg, portMAX_DELAY) == pdTRUE)
        {
            drv->show_text(incoming_msg.row, incoming_msg.text);
        }
    }
}
```

The concrete `drivers/ic/ssd1306.h` include is gone from the task layer.

### 3c. `main/main.c` wiring

Old:

```c
static task_sensor_ctx_t   g_task_sensor_ctx;
static task_actuator_ctx_t g_task_actuator_ctx;

extern void task_display(void *pvParameters);
...
    ESP_ERROR_CHECK(ssd1306_driver.init(&display_cfg));
...
    xTaskCreate(task_display, "task_display", 3072, NULL, 4, NULL);
```

New:

```c
static task_sensor_ctx_t   g_task_sensor_ctx;
static task_actuator_ctx_t g_task_actuator_ctx;
static task_display_ctx_t  g_task_display_ctx;
...
    ESP_ERROR_CHECK(ssd1306_driver.init(&display_cfg));

    g_task_display_ctx.driver = &ssd1306_driver;
...
    xTaskCreate(task_display, "task_display", 3072, &g_task_display_ctx, 4, NULL);
```

---

## 4. Transport: wrap network_manager behind transport_driver_t

### 4a. `main/hal/transport_driver.h` (contract fix)

The `cmd_cb` member was declared with function type, which is invalid as a struct
member. No unit had included this header before, so it had never been compiled.
The adapter now includes it, so the member was corrected to a function pointer.

Old:

```c
typedef struct {
    const char        *broker_uri;
    const char        *device_id;
    transport_cmd_cb_t cmd_cb;
    void              *extra;
} transport_config_t;
```

New:

```c
typedef struct {
    const char         *broker_uri;
    const char         *device_id;
    transport_cmd_cb_t *cmd_cb;
    void               *extra;
} transport_config_t;
```

### 4b. New files `main/drivers/transport/transport_mqtt.{h,c}`

The adapter implements `transport_driver_t` by delegating to network_manager.
Header exposes the instance and a WiFi-credential payload for `extra`:

```c
typedef struct {
    const char *wifi_ssid;
    const char *wifi_pass;
} transport_mqtt_config_t;

extern const transport_driver_t transport_mqtt_driver;
```

Implementation (abridged):

```c
static esp_err_t transport_mqtt_init(const transport_config_t *cfg) {
    if (cfg == NULL || cfg->broker_uri == NULL || cfg->device_id == NULL || cfg->extra == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    const transport_mqtt_config_t *ext = (const transport_mqtt_config_t *)cfg->extra;

    system_config_t sys_cfg;
    memset(&sys_cfg, 0, sizeof(sys_cfg));
    strlcpy(sys_cfg.wifi_ssid, ext->wifi_ssid ? ext->wifi_ssid : "", sizeof(sys_cfg.wifi_ssid));
    strlcpy(sys_cfg.wifi_pass, ext->wifi_pass ? ext->wifi_pass : "", sizeof(sys_cfg.wifi_pass));
    strlcpy(sys_cfg.broker_uri, cfg->broker_uri, sizeof(sys_cfg.broker_uri));
    strlcpy(sys_cfg.device_id, cfg->device_id, sizeof(sys_cfg.device_id));

    network_manager_set_command_cb(cfg->cmd_cb);

    esp_err_t err = network_manager_init(&sys_cfg);
    if (err != ESP_OK) return err;
    return network_manager_start();
}

const transport_driver_t transport_mqtt_driver = {
    .name         = "MQTT_WIFI",
    .init         = transport_mqtt_init,
    .publish      = transport_mqtt_publish,     // -> network_manager_publish
    .subscribe    = transport_mqtt_subscribe,   // -> network_manager_subscribe
    .is_connected = transport_mqtt_is_connected,// -> network_manager_is_connected
    .deinit       = transport_mqtt_deinit,
};
```

### 4c. `main/network/network_manager.{h,c}` additions

Three functions were added to make network_manager usable as a transport
implementation: a generic publish, a subscribe, and a command-callback setter.

```c
esp_err_t network_manager_publish(const char *topic, const char *payload, size_t length);
esp_err_t network_manager_subscribe(const char *topic);
void      network_manager_set_command_cb(transport_cmd_cb_t *cb);
```

### 4d. `main/main.c` wiring

Old:

```c
    /* [8] Network Manager ---------------------------------------------------- */
    ESP_LOGI(TAG, "Initializing Network Manager...");
    ESP_ERROR_CHECK(network_manager_init(&sys_cfg));
    ESP_ERROR_CHECK(network_manager_start());
```

New:

```c
    /* [8] Transport (WiFi + MQTT) -------------------------------------------- */
    ESP_LOGI(TAG, "Initializing transport (WiFi + MQTT)...");
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
```

The system supervisor still publishes its heartbeat through
`network_manager_publish_health`, which is unchanged.

---

## 5. Command interpretation moved to orchestration

Inbound MQTT command parsing was previously hardcoded inside network_manager,
which reached directly into `g_queue_actuator`. The transport now forwards raw
payloads to a callback, and the orchestration layer owns the interpretation.

### 5a. `main/network/network_manager.c`

Old:

```c
        case MQTT_EVENT_DATA:
            ...
            if (strncmp(event->data, "ON", event->data_len) == 0) {
                bool state = true;
                xQueueSend(g_queue_actuator, &state, 0);
                ESP_LOGI(TAG, "Remote control action: turned actuator ON");
            }
            else if (strncmp(event->data, "OFF", event->data_len) == 0) {
                bool state = false;
                xQueueSend(g_queue_actuator, &state, 0);
                ESP_LOGI(TAG, "Remote control action: turned actuator OFF");
            }
            break;
```

New:

```c
        case MQTT_EVENT_DATA:
            ...
            if (s_cmd_cb != NULL) {
                s_cmd_cb(event->topic, (const uint8_t *)event->data, (size_t)event->data_len);
            }
            break;
```

The `extern QueueHandle_t g_queue_actuator;` declaration was removed from
network_manager, replaced by `static transport_cmd_cb_t *s_cmd_cb = NULL;`.

### 5b. `main/main.c` callback

New:

```c
static void app_transport_command(const char *topic, const uint8_t *data, size_t data_len) {
    (void)topic;
    if (strncmp((const char *)data, "ON", data_len) == 0) {
        bool state = true;
        xQueueSend(g_queue_actuator, &state, 0);
        ESP_LOGI(TAG, "Remote control action: turned actuator ON");
    } else if (strncmp((const char *)data, "OFF", data_len) == 0) {
        bool state = false;
        xQueueSend(g_queue_actuator, &state, 0);
        ESP_LOGI(TAG, "Remote control action: turned actuator OFF");
    }
}
```

The comparison logic matches the previous behavior exactly, including the use of
`data_len` as the strncmp length.

---

## 6. Unit test updates

`test_ads1115.c` and `test_mcp23017.c` called drivers through `*_get_driver()`.
They now call the public functions directly. `test_sensor_ads1115.c` was not
functionally affected, since it exercises the driver through the `sensor_driver_t`
HAL contract, which is unchanged; only a stale comment was corrected.

Old (`test_ads1115.c`):

```c
static const ads1115_driver_t *s_driver;
...
    s_driver = ads1115_get_driver();
...
    TEST_ASSERT_EQUAL(ESP_OK, s_driver->init(&s_dev, &s_cfg));
```

New:

```c
    TEST_ASSERT_EQUAL(ESP_OK, ads1115_init(&s_dev, &s_cfg));
```

Old (`test_mcp23017.c`):

```c
static const mcp23017_driver_t *s_driver;
...
    s_driver = mcp23017_get_driver();
...
    TEST_ASSERT_EQUAL(ESP_OK, s_driver->write_pin(&s_dev, MCP23017_PORT_A, 3, true));
```

New:

```c
    TEST_ASSERT_EQUAL(ESP_OK, mcp23017_write_pin(&s_dev, MCP23017_PORT_A, 3, true));
```

---

## 7. Build system

`main/CMakeLists.txt` gained the transport source and include directory:

```
"drivers/transport/transport_mqtt.c"
...
"drivers/transport"
```

---

## Files changed

New:
- `main/tasks/task_display.h`
- `main/drivers/transport/transport_mqtt.h`
- `main/drivers/transport/transport_mqtt.c`

Modified:
- `main/drivers/ic/ads1115.h`, `main/drivers/ic/ads1115.c`
- `main/drivers/ic/mcp23017.h`, `main/drivers/ic/mcp23017.c`
- `main/drivers/sensor/sensor_ads1115.c`
- `main/tasks/task_display.c`
- `main/hal/transport_driver.h`
- `main/network/network_manager.h`, `main/network/network_manager.c`
- `main/main.c`
- `main/CMakeLists.txt`
- `test/test_ads1115/test_ads1115.c`
- `test/test_mcp23017/test_mcp23017.c`
- `test/test_sensor_ads1115/test_sensor_ads1115.c`

## Verification

- `idf.py build`: success. App binary 0xea940 bytes, 8% free in the 1 MB app partition.
- `pio test -e native`: 59 test cases, 59 passed.
- Runtime networking behavior (broker connect, publish, inbound command routing)
  is compile-verified only. On-hardware validation against a broker is still needed.

## Follow-up items not covered here

- The MCP23017 driver is de-vtabled but still has no `output_driver_t` adapter and
  is not wired into any task.
- The `system_ota` direct push to `g_queue_display` remains open (Decision #4).
- R&D artifacts in `main.c` (ota_test_task, hardcoded credentials) are unchanged.
