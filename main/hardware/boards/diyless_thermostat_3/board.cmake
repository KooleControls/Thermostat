# ──────────────────────────────────────────────────────────────
# Board fragment: DIYLESS OpenTherm Thermostat 3 (hw rev v3.3)
#   ESP32-S3 (QFN56, 8 MB flash, 8 MB octal PSRAM) · 4.0" 480x480 IPS
#   ST7701S · GT911 capacitive touch · AHT20 on the shared I2C bus ·
#   STM32L051 co-processor (owns the OpenTherm PHY) · no user LED.
#
# Build with:  idf.py set-target esp32s3   (once)   then   idf.py build
#
# Flash/PSRAM/console config lives in this folder's sdkconfig.defaults.
# Component deps are NOT set here — managed deps go in main/idf_component.yml,
# IDF built-ins in COMPONENT_REQUIRES (see note in main/CMakeLists.txt).
# ──────────────────────────────────────────────────────────────

list(APPEND BOARD_SOURCES "${CMAKE_CURRENT_LIST_DIR}/Board.cpp")
