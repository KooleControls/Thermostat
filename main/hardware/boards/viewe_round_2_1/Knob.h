#pragma once

#include "BoardConfig.h"
#include "driver/gpio.h"
#include "drivers/RotaryEncoder.h"

// ──────────────────────────────────────────────────────────────
// Rotary knob for the VIEWE UEDX48480021-MD80ET: a quadrature encoder decoded
// by the PCNT peripheral (via the shared RotaryEncoder driver), plus a
// push-button on a direct GPIO (active-low; the board has an external pull-up).
// Exposed to LVGL as an ENCODER indev. CreateLvglIndev() must be called from
// the LVGL task (under lvgl_port_lock).
// ──────────────────────────────────────────────────────────────

class Knob
{
public:
    bool Init()
    {
        gpio_config_t btn = {};
        btn.mode = GPIO_MODE_INPUT;
        btn.pin_bit_mask = 1ULL << BoardConfig::ENCODER_PIN_SW;
        btn.pull_up_en = GPIO_PULLUP_ENABLE;
        gpio_config(&btn);

        RotaryEncoderConfig cfg{};
        cfg.pin_a = BoardConfig::ENCODER_PIN_A;
        cfg.pin_b = BoardConfig::ENCODER_PIN_B;
        cfg.button_pressed = []() -> bool {
            return gpio_get_level((gpio_num_t)BoardConfig::ENCODER_PIN_SW) == 0;  // active-low
        };
        return enc_.Init(cfg);
    }

    // Create the LVGL encoder indev. Call under lvgl_port_lock.
    lv_indev_t *CreateLvglIndev() { return enc_.CreateLvglIndev(); }

private:
    RotaryEncoder enc_;
};
