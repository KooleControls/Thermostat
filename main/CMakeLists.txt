set(SOURCE_FILES_LIST
    "main.cpp"
    "Application/NetworkManager/NetworkManager.cpp"
    "Application/NetworkManager/NetworkInterface.cpp"
    "Application/NetworkManager/WiFiInterface.cpp"
    "Application/NetworkManager/DnsConfiguration.cpp"
    "Application/WebServerManager/WebServerManager.cpp"
    "Application/WebServerManager/StaticFileHandler.cpp"
    "Application/WebServerManager/WebSocketHandler.cpp"
    "Application/CommandManager/CommandManager.cpp"
    "Application/UpdateManager/UpdateManager.cpp"
    "Application/SettingsManager/SettingsManager.cpp"
    "Application/ConsoleManager/ConsoleManager.cpp"
    "Application/TimeManager/TimeManager.cpp"
    "Application/MqttManager/MqttManager.cpp"
    "Application/BleManager/BleManager.cpp"
    "Application/ClimateManager/ClimateManager.cpp"
    "Application/ClimateManager/TemperatureSensor.cpp"
    "Application/DeviceManager/DeviceManager.cpp"
    "Application/DisplayManager/DisplayManager.cpp"
    "Application/DisplayManager/ThermostatScreen.cpp"
    "Application/DisplayManager/PinScreen.cpp"
    "Application/DisplayManager/GatewayScreen.cpp"
    "Application/DisplayManager/fonts/font_temp_96.c"
    "Application/DisplayManager/fonts/font_modes_40.c"
    "Application/HomeAssistantManager/HomeAssistantManager.cpp"
    "lib/system/DateTime.cpp"
    "lib/system/TimeSpan.cpp"
)

set(INCLUDE_DIRS_LIST
    .
    "Application"
    "Application/NetworkManager"
    "Application/WebServerManager"
    "Application/CommandManager"
    "Application/UpdateManager"
    "Application/SettingsManager"
    "Application/ConsoleManager"
    "Application/TimeManager"
    "Application/MqttManager"
    "Application/BleManager"
    "Application/ClimateManager"
    "Application/DeviceManager"
    "Application/DisplayManager"
    "Application/DisplayManager/fonts"
    "Application/HomeAssistantManager"
    "hardware"
    "lib"
    "lib/common"
    "lib/rtos"
    "lib/json"
    "lib/system"
)

set(COMPONENT_REQUIRES
    esp_timer
    esp_netif
    esp_event
    esp_wifi
    nvs_flash
    esp_http_server
    esp_app_format
    app_update
    esp_partition
    fatfs
    wear_levelling
    bt
    esp_driver_gpio
    esp_driver_i2c
    esp_driver_tsens
    esp_lcd
    mdns
    espressif__mqtt
    lvgl__lvgl
    espressif__esp_lvgl_port
    espressif__esp_lcd_touch
    espressif__esp_lcd_touch_gt911
)

# Register component with ESP-IDF
idf_component_register(
    SRCS ${SOURCE_FILES_LIST}
    INCLUDE_DIRS ${INCLUDE_DIRS_LIST}
    REQUIRES ${COMPONENT_REQUIRES}
)

# The generated LVGL font (font_temp_96.c) includes "lvgl.h" only when
# LV_LVGL_H_INCLUDE_SIMPLE is defined; otherwise it uses "lvgl/lvgl.h".
target_compile_definitions(${COMPONENT_LIB} PRIVATE LV_LVGL_H_INCLUDE_SIMPLE)
