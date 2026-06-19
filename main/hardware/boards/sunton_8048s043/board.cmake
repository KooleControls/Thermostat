# ──────────────────────────────────────────────────────────────
# Board fragment: Sunton ESP32-8048S043
#   ESP32-S3 N16R8 · 4.3" 800x480 16-bit RGB-parallel panel
#   (raw esp_lcd_new_rgb_panel) · GT911 capacitive touch · no rotary knob
#
# A board fragment may append to BOARD_REQUIRES / BOARD_SOURCES and set
# BOARD_HAS_KNOB. Shared requires/sources live in main/CMakeLists.txt.
# ──────────────────────────────────────────────────────────────

list(APPEND BOARD_REQUIRES
    espressif__esp_lcd_touch_gt911
)

set(BOARD_HAS_KNOB FALSE)
