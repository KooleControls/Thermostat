# ──────────────────────────────────────────────────────────────
# Board fragment: DIYLESS OpenTherm Thermostat 3 (hw rev v3.3)
#   ESP32-S3 (QFN56, 8 MB flash, 8 MB octal PSRAM) · 4.0" 480x480 IPS
#   ST7701S (3-wire SPI init + 16-bit RGB565) · GT911 capacitive touch
#   AHT20 temp/humidity on the shared I2C bus · STM32L051 co-processor
#   (owns the OpenTherm PHY) · no rotary knob.
#
# Pin map is authoritative — supplied by DIYLESS (Ihor Melnyk) via the official
# ESPHome reference config diyless-thermostat-3.yaml.
#
# Unlike the Waveshare ST7701 board, LCD reset / SPI-CS / backlight are all
# direct GPIOs (no IO expander), so the Display/Touch HAL is simpler.
#
# All board headers are header-only (no BOARD_SOURCES). RGB panel is the
# default (BOARD_PANEL_RGB stays TRUE). Flash size (8 MB) + the matching
# partition table are set in sdkconfig.defaults.diyless_thermostat_3.
#
# Component deps (esp_lcd_st7701, esp_lcd_panel_io_additions,
# esp_lcd_touch_gt911) are already managed deps in main/idf_component.yml.
# ──────────────────────────────────────────────────────────────

set(BOARD_HAS_KNOB FALSE)
