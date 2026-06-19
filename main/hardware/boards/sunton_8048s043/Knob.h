#pragma once

// ──────────────────────────────────────────────────────────────
// Knob (rotary encoder) HAL — stub for the Sunton ESP32-8048S043.
//
// This board has no rotary input, so this is a no-op placeholder that
// satisfies the board HAL contract. DisplayManager only instantiates a Knob
// and registers an LVGL encoder indev when BOARD_HAS_KNOB is defined (it is
// not, for this board), so none of this is actually used here.
// ──────────────────────────────────────────────────────────────

class Knob
{
public:
    bool Init() { return false; }  // no knob on this board
};
