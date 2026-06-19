# ──────────────────────────────────────────────────────────────
# Board fragment: Elecrow CrowPanel 2.1" HMI Rotary Display
#   ESP32-S3R8 · 2.1" 480x480 round IPS · ST7701S (3-wire SPI init + RGB)
#   CST826 capacitive touch · PCF8574 IO expander · rotary knob (encoder + SW)
#
# All board headers are header-only, so no BOARD_SOURCES are needed.
# ──────────────────────────────────────────────────────────────

list(APPEND BOARD_REQUIRES
    espressif__esp_lcd_st7701
    espressif__esp_lcd_panel_io_additions   # 3-wire SPI panel IO for ST7701 init
    espressif__esp_lcd_touch_cst816s         # CST826 (CST816 family)
    esp_driver_pcnt                          # quadrature encoder decode
)

set(BOARD_HAS_KNOB TRUE)
