#pragma once

#include "lvgl.h"
#include <cstdint>

// One place for the on-screen look, so restyling stays purely presentational
// (see docs/backlog/2026-07-09-ui-visual-polish.md — that pass is Bas's canvas
// and should not have to touch layout or control code).
//
// Colours are functions, not constants: lv_color_hex() is an inline function,
// so namespace-scope objects would need dynamic initialization.
namespace UiTheme
{
    inline lv_color_t Bg()      { return lv_color_hex(0x000000); }   // screen background
    inline lv_color_t Surface() { return lv_color_hex(0x1C1C1E); }   // menu rows, cards
    inline lv_color_t Text()    { return lv_color_hex(0xFFFFFF); }
    inline lv_color_t TextDim() { return lv_color_hex(0x888888); }
    inline lv_color_t Accent()  { return lv_color_hex(0x2F80ED); }
    inline lv_color_t Danger()  { return lv_color_hex(0xE5484D); }

    /// A style selector is a part OR'd with a state, but the build is gnu++26,
    /// where a bitwise OR between two different enum types is an error — go
    /// through the selector type instead.
    inline constexpr lv_style_selector_t Sel(lv_part_t part, lv_state_t state)
    {
        return static_cast<lv_style_selector_t>(part) | static_cast<lv_style_selector_t>(state);
    }

    constexpr int32_t Pad = 16;
    constexpr int32_t HeaderH = 72;   // title bar height on non-home screens
    constexpr int32_t RowH = 76;      // menu row height — a comfortable touch target
    constexpr int32_t IconBtn = 64;   // gear / close / back hit area
}
