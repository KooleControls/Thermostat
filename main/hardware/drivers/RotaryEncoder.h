#pragma once

#include "driver/pulse_cnt.h"
#include "esp_log.h"
#include "lvgl.h"

// ──────────────────────────────────────────────────────────────
// Reusable driver — quadrature rotary encoder decoded in hardware by the PCNT
// peripheral, exposed to LVGL as an ENCODER indev (rotation moves focus /
// adjusts the focused widget; press activates it).
//
// The board supplies the two encoder GPIOs and a button-read callback (the
// push-button lives on different hardware per board — a direct GPIO on one,
// an IO-expander pin on another). The callback returns true while pressed; it
// must be a plain function or a captureless lambda (stored as a function
// pointer, no allocation).
//
// CreateLvglIndev() must be called from the LVGL task (under lvgl_port_lock).
// Screens must add their focusable widgets to the default LVGL group for the
// encoder to navigate — DisplayManager installs that group in InitKnob().
// ──────────────────────────────────────────────────────────────

struct RotaryEncoderConfig
{
    int pin_a = -1;
    int pin_b = -1;
    int counts_per_detent = 4;          // quadrature counts per detent (EC11-style)
    bool (*button_pressed)() = nullptr; // true while pressed; nullptr → never pressed
};

class RotaryEncoder
{
    static constexpr const char *TAG = "Knob";

public:
    bool Init(const RotaryEncoderConfig &cfg)
    {
        cfg_ = cfg;

        pcnt_unit_config_t unit_cfg = {};
        unit_cfg.low_limit = -1000;
        unit_cfg.high_limit = 1000;
        if (pcnt_new_unit(&unit_cfg, &unit_) != ESP_OK)
        {
            ESP_LOGW(TAG, "pcnt_new_unit failed");
            return false;
        }

        pcnt_glitch_filter_config_t filter = {};
        filter.max_glitch_ns = 1000;
        pcnt_unit_set_glitch_filter(unit_, &filter);

        pcnt_chan_config_t chan_a_cfg = {};
        chan_a_cfg.edge_gpio_num = cfg_.pin_a;
        chan_a_cfg.level_gpio_num = cfg_.pin_b;
        pcnt_channel_handle_t chan_a = nullptr;
        pcnt_new_channel(unit_, &chan_a_cfg, &chan_a);
        pcnt_channel_set_edge_action(chan_a, PCNT_CHANNEL_EDGE_ACTION_DECREASE,
                                     PCNT_CHANNEL_EDGE_ACTION_INCREASE);
        pcnt_channel_set_level_action(chan_a, PCNT_CHANNEL_LEVEL_ACTION_KEEP,
                                      PCNT_CHANNEL_LEVEL_ACTION_INVERSE);

        pcnt_chan_config_t chan_b_cfg = {};
        chan_b_cfg.edge_gpio_num = cfg_.pin_b;
        chan_b_cfg.level_gpio_num = cfg_.pin_a;
        pcnt_channel_handle_t chan_b = nullptr;
        pcnt_new_channel(unit_, &chan_b_cfg, &chan_b);
        pcnt_channel_set_edge_action(chan_b, PCNT_CHANNEL_EDGE_ACTION_INCREASE,
                                     PCNT_CHANNEL_EDGE_ACTION_DECREASE);
        pcnt_channel_set_level_action(chan_b, PCNT_CHANNEL_LEVEL_ACTION_KEEP,
                                      PCNT_CHANNEL_LEVEL_ACTION_INVERSE);

        pcnt_unit_enable(unit_);
        pcnt_unit_clear_count(unit_);
        pcnt_unit_start(unit_);
        return true;
    }

    // Create the LVGL encoder indev. Call under lvgl_port_lock.
    lv_indev_t *CreateLvglIndev()
    {
        lv_indev_t *indev = lv_indev_create();
        lv_indev_set_type(indev, LV_INDEV_TYPE_ENCODER);
        lv_indev_set_read_cb(indev, &RotaryEncoder::ReadCb);
        lv_indev_set_user_data(indev, this);
        return indev;
    }

    bool ok() const { return unit_ != nullptr; }

private:
    static void ReadCb(lv_indev_t *indev, lv_indev_data_t *data)
    {
        auto *self = static_cast<RotaryEncoder *>(lv_indev_get_user_data(indev));
        self->Read(data);
    }

    void Read(lv_indev_data_t *data)
    {
        int count = 0;
        if (unit_) pcnt_unit_get_count(unit_, &count);
        int detents = (count - lastCount_) / cfg_.counts_per_detent;
        if (detents != 0)
        {
            data->enc_diff = (int16_t)detents;
            lastCount_ += detents * cfg_.counts_per_detent;
        }
        else
        {
            data->enc_diff = 0;
        }

        bool pressed = cfg_.button_pressed ? cfg_.button_pressed() : false;
        data->state = pressed ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
    }

    RotaryEncoderConfig cfg_{};
    pcnt_unit_handle_t unit_ = nullptr;
    int lastCount_ = 0;
};
