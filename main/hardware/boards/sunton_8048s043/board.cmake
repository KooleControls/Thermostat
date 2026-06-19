# ──────────────────────────────────────────────────────────────
# Board fragment: Sunton ESP32-8048S043
#   ESP32-S3 N16R8 · 4.3" 800x480 16-bit RGB-parallel panel
#   (raw esp_lcd_new_rgb_panel) · GT911 capacitive touch · no rotary knob
#
# A board fragment may set BOARD_SOURCES and BOARD_HAS_KNOB. Component deps are
# NOT set here (see the note in main/CMakeLists.txt): the GT911 touch driver is
# a managed dep in main/idf_component.yml.
# ──────────────────────────────────────────────────────────────

set(BOARD_HAS_KNOB FALSE)
