#pragma once
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

// 96px Montserrat subset (digits, '.', '-', '+', '°') for the big temperature.
// Generated with lv_font_conv from lvgl/scripts/built_in_font/Montserrat-Medium.ttf.
extern const lv_font_t font_temp_96;

// 40px Font Awesome subset for the mode icons. Glyph codepoints:
//   0xF011 power-off, 0xF021 arrows-rotate (auto), 0xF06D fire, 0xF2DC snowflake.
extern const lv_font_t font_modes_40;

#ifdef __cplusplus
}
#endif
