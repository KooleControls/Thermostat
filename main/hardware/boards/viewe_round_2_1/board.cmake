# ──────────────────────────────────────────────────────────────
# Board fragment: VIEWE UEDX48480021-MD80ET
#   ESP32-S3R8 · 2.1" 480x480 round IPS · ST7701S (3-wire SPI init + RGB)
#   CST826 capacitive touch · rotary knob (encoder + button) · NO IO expander
#
# Component deps are NOT set here (see the note in main/CMakeLists.txt): the
# panel/touch drivers (esp_lcd_st7701, esp_lcd_panel_io_additions,
# esp_lcd_touch_cst816s) are managed deps in main/idf_component.yml, and the
# IDF built-in esp_driver_pcnt (encoder) is in main's common REQUIRES.
# ──────────────────────────────────────────────────────────────

set(BOARD_HAS_KNOB TRUE)
