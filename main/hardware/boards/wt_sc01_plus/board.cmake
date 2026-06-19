# ──────────────────────────────────────────────────────────────
# Board fragment: Wireless-Tag WT-SC01 Plus
#   ESP32-S3-WROOM-1 N16R8 · 3.5" 320x480 ST7796 (8-bit i80/8080 parallel)
#   FT6336U capacitive touch (FT5x06 family) · no rotary knob
#
# All board headers are header-only, so no BOARD_SOURCES are needed.
#
# This is a COMMAND-DRIVEN panel (not RGB), so it sets BOARD_PANEL_RGB FALSE:
# DisplayManager then registers it via the regular lvgl_port_add_disp path
# (esp_lcd_panel_draw_bitmap flush) instead of lvgl_port_add_disp_rgb.
#
# Component deps are NOT set here (see the note in main/CMakeLists.txt): the
# panel/touch drivers (esp_lcd_st7796, esp_lcd_touch_ft5x06) are managed deps
# in main/idf_component.yml.
# ──────────────────────────────────────────────────────────────

set(BOARD_HAS_KNOB FALSE)
set(BOARD_PANEL_RGB FALSE)
