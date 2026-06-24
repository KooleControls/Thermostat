# ──────────────────────────────────────────────────────────────
# Board fragment: Waveshare ESP32-S3-Touch-LCD-4
#   ESP32-S3R8 · 4.0" 480x480 IPS · ST7701 (3-wire SPI init + 16-bit RGB)
#   GT911 capacitive touch · CH32V003 IO expander (LCD/touch reset, system
#   power, backlight PWM) · no rotary knob
#
# All board headers are header-only, so no BOARD_SOURCES are needed. RGB panel
# is the default (BOARD_PANEL_RGB stays TRUE).
#
# Component deps are NOT set here (see the note in main/CMakeLists.txt): the
# panel/touch/expander drivers (esp_lcd_st7701, esp_lcd_panel_io_additions,
# esp_lcd_touch_gt911, waveshare/custom_io_expander_ch32v003) are managed deps
# in main/idf_component.yml.
# ──────────────────────────────────────────────────────────────

set(BOARD_HAS_KNOB FALSE)
