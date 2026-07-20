# main/CMakeLists.txt

idf_component_register(
    SRCS
        "main.c"
        "bus/i2c_bus.c"
        "system/system_config.c"
        "system/system_log.c"   
        "system/system_supervisor.c"
        "system/system_ota.c"
        
        # --- FreeRTOS Orchestrator Tasks ---
        "tasks/task_sensor.c"
        "tasks/task_actuator.c"
        "tasks/task_display.c"
        
        # --- Hardware Drivers (HAL Implementations) ---
        "drivers/sensor/sensor_dummy.c"
        "drivers/output/actuator_dummy.c"
        "drivers/ic/ssd1306.c"
        "drivers/ic/ads1115.c"
        "drivers/ic/mcp23017.c"
        "drivers/sensor/sensor_ads1115.c" 

        # --- Network & Communication ---
        "network/network_manager.c"

    INCLUDE_DIRS 
        "."
        "bus"
        "system"
        "tasks"
        "drivers"
        "drivers/ic"
        "drivers/sensor"
        "drivers/output"
        "network"
        
    PRIV_REQUIRES 
        # --- Core ESP-IDF Network & Storage Components ---
        nvs_flash
        driver
        esp_adc
        esp_wifi
        esp_event
        esp_netif
        mqtt
        lwip
        
        # --- OTA & HTTP Components ---
        esp_https_ota 
        esp_http_client
        app_update
)