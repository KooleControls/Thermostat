#pragma once

#include "BoardConfig.h"
#include "Pcf8574.h"
#include "driver/pulse_cnt.h"
#include "esp_log.h"
#include "lvgl.h"

// ──────────────────────────────────────────────────────────────
// Rotary knob for the Elecrow CrowPanel 2.1": a quadrature encoder
// (decoded in hardware by the PCNT peripheral) plus a push-button that hangs
// off the PCF8574 expander (active-low).
//
// Exposed to LVGL as an ENCODER indev: rotation moves focus / adjusts the
// focused widget, press activates it. CreateLvglIndev() must be called from
// the LVGL task (under lvgl_port_lock); DisplayManager does this when
// BOARD_HAS_KNOB is defined.
//
// ⚠ For the encoder to navigate, screens must add their focusable widgets to
// the default LVGL group (lv_group_get_default()). DisplayManager creates and
// installs that group in InitKnob().
// ──────────────────────────────────────────────────────────────

class Knob
{
    static constexpr const char *TAG = "Knob";
    // Quadrature counts per physical detent for typical EC11-style encoders.
    static constexpr int COUNTS_PER_DETENT = 4;

public:
    bool Init()
    {
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
        chan_a_cfg.edge_gpio_num = BoardConfig::ENCODER_PIN_A;
        chan_a_cfg.level_gpio_num = BoardConfig::ENCODER_PIN_B;
        pcnt_channel_handle_t chan_a = nullptr;
        pcnt_new_channel(unit_, &chan_a_cfg, &chan_a);
        pcnt_channel_set_edge_action(chan_a, PCNT_CHANNEL_EDGE_ACTION_DECREASE,
                                     PCNT_CHANNEL_EDGE_ACTION_INCREASE);
        pcnt_channel_set_level_action(chan_a, PCNT_CHANNEL_LEVEL_ACTION_KEEP,
                                      PCNT_CHANNEL_LEVEL_ACTION_INVERSE);

        pcnt_chan_config_t chan_b_cfg = {};
        chan_b_cfg.edge_gpio_num = BoardConfig::ENCODER_PIN_B;
        chan_b_cfg.level_gpio_num = BoardConfig::ENCODER_PIN_A;
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
        lv_indev_set_read_cb(indev, &Knob::ReadCb);
        lv_indev_set_user_data(indev, this);
        return indev;
    }

private:
    static void ReadCb(lv_indev_t *indev, lv_indev_data_t *data)
    {
        auto *self = static_cast<Knob *>(lv_indev_get_user_data(indev));
        self->Read(data);
    }

    void Read(lv_indev_data_t *data)
    {
        int count = 0;
        if (unit_) pcnt_unit_get_count(unit_, &count);
        int detents = (count - lastCount_) / COUNTS_PER_DETENT;
        if (detents != 0)
        {
            data->enc_diff = (int16_t)detents;
            lastCount_ += detents * COUNTS_PER_DETENT;
        }
        else
        {
            data->enc_diff = 0;
        }

        bool pressed = false;
        if (BoardPcf().ok())
            pressed = !BoardPcf().GetPin(BoardConfig::PCF_ENCODER_SW);  // active-low
        data->state = pressed ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
    }

    pcnt_unit_handle_t unit_ = nullptr;
    int lastCount_ = 0;
};
