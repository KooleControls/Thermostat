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
    esp_driver_pcnt          # Elecrow rotary-knob quadrature decode (unused on Sunton)
    esp_lcd
    mdns
    espressif__mqtt
    lvgl__lvgl
    espressif__esp_lvgl_port
    espressif__esp_lcd_touch
)

# ── Board selection ────────────────────────────────────────────
# Pick the target board with -DBOARD=<name>; defaults to the Sunton panel so
# existing builds are unchanged. Each board lives in hardware/boards/<name>/
# with its own BoardConfig.h, Display.h, Touch.h, Knob.h and a board.cmake
# fragment that may append to BOARD_REQUIRES / BOARD_SOURCES and set
# BOARD_HAS_KNOB.
set(BOARD "sunton_8048s043" CACHE STRING "Target board (folder under main/hardware/boards/)")

set(BOARD_DIR "${CMAKE_CURRENT_LIST_DIR}/hardware/boards/${BOARD}")
if(NOT EXISTS "${BOARD_DIR}/board.cmake")
    file(GLOB _available_boards RELATIVE "${CMAKE_CURRENT_LIST_DIR}/hardware/boards"
         "${CMAKE_CURRENT_LIST_DIR}/hardware/boards/*")
    message(FATAL_ERROR "Unknown BOARD '${BOARD}'. Available boards: ${_available_boards}")
endif()

set(BOARD_SOURCES "")
set(BOARD_HAS_KNOB FALSE)
# Panel kind: TRUE for RGB-direct-framebuffer panels (the default, used by the
# three RGB boards), FALSE for command-driven panels (i80/SPI, e.g. WT-SC01
# Plus). DisplayManager branches on the BOARD_PANEL_RGB define to register the
# correct LVGL display path (lvgl_port_add_disp_rgb vs lvgl_port_add_disp).
set(BOARD_PANEL_RGB TRUE)
include("${BOARD_DIR}/board.cmake")

# NOTE on component REQUIRES: ESP-IDF resolves a component's REQUIRES in a
# separate "expansion" pass that runs this CMakeLists in script mode WITHOUT
# the -DBOARD cache var, so board.cmake can't switch REQUIRES per board there
# (it would always see the default board). So component deps are handled by the
# two channels that DO work regardless of board:
#   • managed deps (panel/touch drivers) -> main/idf_component.yml
#   • IDF built-ins (e.g. esp_driver_pcnt) -> COMPONENT_REQUIRES above
# board.cmake is therefore limited to BOARD_SOURCES and BOARD_HAS_KNOB, which
# are evaluated in the normal configure pass where BOARD is visible.

# The selected board folder is the ONLY one on the include path, so headers
# included by name (BoardConfig.h / Display.h / Touch.h / Knob.h) resolve to it.
list(APPEND INCLUDE_DIRS_LIST "hardware/boards/${BOARD}")
list(APPEND SOURCE_FILES_LIST ${BOARD_SOURCES})
message(STATUS "Building for BOARD=${BOARD} (knob=${BOARD_HAS_KNOB}, rgb_panel=${BOARD_PANEL_RGB})")

# Register component with ESP-IDF
idf_component_register(
    SRCS ${SOURCE_FILES_LIST}
    INCLUDE_DIRS ${INCLUDE_DIRS_LIST}
    REQUIRES ${COMPONENT_REQUIRES}
)

# The generated LVGL font (font_temp_96.c) includes "lvgl.h" only when
# LV_LVGL_H_INCLUDE_SIMPLE is defined; otherwise it uses "lvgl/lvgl.h".
target_compile_definitions(${COMPONENT_LIB} PRIVATE LV_LVGL_H_INCLUDE_SIMPLE)

# Board identity defines for the (few) code-level conditionals.
string(TOUPPER "${BOARD}" BOARD_UPPER)
target_compile_definitions(${COMPONENT_LIB} PRIVATE BOARD_${BOARD_UPPER})
if(BOARD_HAS_KNOB)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE BOARD_HAS_KNOB)
endif()
if(BOARD_PANEL_RGB)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE BOARD_PANEL_RGB)
endif()
