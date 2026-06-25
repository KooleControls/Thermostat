#pragma once

#include "BoardConfig.h"
#include "Pcf8574.h"
#include "drivers/RotaryEncoder.h"

// ──────────────────────────────────────────────────────────────
// Rotary knob for the Elecrow CrowPanel 2.1": a quadrature encoder (decoded by
// the PCNT peripheral via the shared RotaryEncoder driver) plus a push-button
// that hangs off the PCF8574 expander (active-low).
//
// Exposed to LVGL as an ENCODER indev. CreateLvglIndev() must be called from
// the LVGL task (under lvgl_port_lock); DisplayManager does this when
// BOARD_HAS_KNOB is defined.
// ──────────────────────────────────────────────────────────────

class Knob
{
public:
    bool Init()
    {
        RotaryEncoderConfig cfg{};
        cfg.pin_a = BoardConfig::ENCODER_PIN_A;
        cfg.pin_b = BoardConfig::ENCODER_PIN_B;
        cfg.button_pressed = []() -> bool {
            return BoardPcf().ok() && !BoardPcf().GetPin(BoardConfig::PCF_ENCODER_SW);
        };
        return enc_.Init(cfg);
    }

    // Create the LVGL encoder indev. Call under lvgl_port_lock.
    lv_indev_t *CreateLvglIndev() { return enc_.CreateLvglIndev(); }

private:
    RotaryEncoder enc_;
};
