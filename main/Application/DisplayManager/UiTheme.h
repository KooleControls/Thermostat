#pragma once

#include "lvgl.h"
#include <cstdint>

// One place for the on-screen look, so restyling stays purely presentational
// (see docs/backlog/2026-07-09-ui-visual-polish.md — that pass is Bas's canvas
// and should not have to touch layout or control code).
//
// Colours are functions, not constants: lv_color_hex() is an inline function,
// so namespace-scope objects would need dynamic initialization. That indirection
// is also what makes a second palette possible — every caller already asks at
// build time rather than baking a literal.
namespace UiTheme
{
    enum class Mode : uint8_t { Dark = 0, Light = 1 };

    // Read by every colour below. Not guarded: it is written once at startup and
    // then only from the LVGL task (the settings toggle), which is the only task
    // that reads it. Changing it restyles nothing by itself — the colours are
    // copied into per-widget local styles at Build() time, so the screens have to
    // be rebuilt after (DisplayManager::Restyle).
    inline Mode g_mode = Mode::Dark;

    inline void SetMode(Mode m) { g_mode = m; }
    inline bool IsLight()       { return g_mode == Mode::Light; }

    inline lv_color_t Pick(uint32_t dark, uint32_t light)
    {
        return lv_color_hex(IsLight() ? light : dark);
    }

    // The light palette is grouped-table shaped: a grey ground with white cards
    // on it, rather than dark's black ground with grey cards. In both the
    // surface reads as *raised* off the background, which is what the screens
    // assume when they put a card on Bg().
    inline lv_color_t Bg()      { return Pick(0x000000, 0xF2F2F7); }   // screen background
    inline lv_color_t Surface() { return Pick(0x1C1C1E, 0xFFFFFF); }   // menu rows, cards
    inline lv_color_t Text()    { return Pick(0xFFFFFF, 0x1C1C1E); }
    inline lv_color_t TextDim() { return Pick(0x888888, 0x8A8A8E); }
    inline lv_color_t Danger()  { return Pick(0xE5484D, 0xD70015); }

    // Deeper on light so a white label still clears 4.5:1 on a filled button —
    // the dark palette's blue is tuned against black and washes out on white.
    inline lv_color_t Accent()  { return Pick(0x2F80ED, 0x0A68D8); }

    /// Label/icon colour for text sitting *on* an Accent() fill, which is not
    /// the same thing as Text(). On dark the two coincide and the distinction is
    /// invisible; on light, Text() is near-black and would sit on blue.
    inline lv_color_t OnAccent() { return lv_color_hex(0xFFFFFF); }

    /// Veil colour for momentary press feedback, used with a low bg_opa. Has to
    /// invert with the palette: any tint of the surface itself is invisible
    /// against a background the surface already resembles.
    inline lv_color_t Press()   { return Pick(0xFFFFFF, 0x000000); }

    // Climate hues — heating and cooling read as warm/cold everywhere they
    // appear (badge, ring, nudge buttons, mode tiles), so they live here rather
    // than being re-picked per widget.
    inline lv_color_t Heat()    { return Pick(0xE03A3A, 0xD92B2B); }
    inline lv_color_t Cool()    { return Pick(0x3B82F6, 0x1D6FE0); }

    // The home readout disc, top and bottom of its vertical gradient. Here
    // rather than in HomeFace because the pair is a palette decision: the disc
    // is lit from the top in both modes, but on light it is a white disc lifting
    // off grey, not a dark one sinking into black.
    inline lv_color_t DiscTop()    { return Pick(0x24262B, 0xFFFFFF); }
    inline lv_color_t DiscBottom() { return Pick(0x141519, 0xE6E6EB); }

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
