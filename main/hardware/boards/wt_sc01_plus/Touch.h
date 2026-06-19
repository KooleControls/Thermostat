#pragma once

#include "BoardConfig.h"
#include "driver/i2c_master.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_touch_ft5x06.h"
#include "esp_log.h"
#include "esp_err.h"

// ──────────────────────────────────────────────────────────────
// FT6336U capacitive touch driver (I2C) for the WT-SC01 Plus.
// The FT6336U is FocalTech FT5x06-compatible (I2C addr 0x38), so it is driven
// by the esp_lcd_touch_ft5x06 component. Init() is best-effort: it returns
// false (and the display keeps working) if the controller can't be reached.
// ──────────────────────────────────────────────────────────────

class Touch
{
    static constexpr const char *TAG = "Touch";

public:
    bool Init()
    {
        i2c_master_bus_config_t bus_cfg = {};
        bus_cfg.i2c_port = I2C_NUM_0;
        bus_cfg.sda_io_num = (gpio_num_t)BoardConfig::TOUCH_PIN_SDA;
        bus_cfg.scl_io_num = (gpio_num_t)BoardConfig::TOUCH_PIN_SCL;
        bus_cfg.clk_source = I2C_CLK_SRC_DEFAULT;
        bus_cfg.glitch_ignore_cnt = 7;
        bus_cfg.flags.enable_internal_pullup = true;

        i2c_master_bus_handle_t i2c_bus = nullptr;
        esp_err_t err = i2c_new_master_bus(&bus_cfg, &i2c_bus);
        if (err != ESP_OK)
        {
            ESP_LOGW(TAG, "I2C bus init failed: %s", esp_err_to_name(err));
            return false;
        }

        // Build the FT5x06 panel-IO config explicitly (C++ doesn't honour the
        // library's designated-initializer macro field order).
        esp_lcd_panel_io_handle_t tp_io = nullptr;
        esp_lcd_panel_io_i2c_config_t tp_io_cfg = {};
        tp_io_cfg.dev_addr = ESP_LCD_TOUCH_IO_I2C_FT5x06_ADDRESS;
        tp_io_cfg.scl_speed_hz = 400000;
        tp_io_cfg.control_phase_bytes = 1;
        tp_io_cfg.dc_bit_offset = 0;
        tp_io_cfg.lcd_cmd_bits = 8;
        tp_io_cfg.flags.disable_control_phase = 1;
        err = esp_lcd_new_panel_io_i2c(i2c_bus, &tp_io_cfg, &tp_io);
        if (err != ESP_OK)
        {
            ESP_LOGW(TAG, "Touch panel IO init failed: %s", esp_err_to_name(err));
            return false;
        }

        esp_lcd_touch_config_t tp_cfg = {};
        // x_max/y_max are the NATIVE (pre-swap, portrait) panel extents; the
        // swap_xy/mirror flags below rotate raw coords into the landscape frame
        // so touch lines up with the rotated display. Mirrors the proven
        // WT32-SC01 sibling-project config (swap_xy + mirror_x).
        tp_cfg.x_max = BoardConfig::LCD_V_RES;  // native portrait width  (320)
        tp_cfg.y_max = BoardConfig::LCD_H_RES;  // native portrait height (480)
        tp_cfg.rst_gpio_num = (gpio_num_t)BoardConfig::TOUCH_PIN_RST;
        tp_cfg.int_gpio_num = (gpio_num_t)BoardConfig::TOUCH_PIN_INT;
        tp_cfg.flags.swap_xy = 1;
        tp_cfg.flags.mirror_x = 1;
        tp_cfg.flags.mirror_y = 0;

        err = esp_lcd_touch_new_i2c_ft5x06(tp_io, &tp_cfg, &touch_);
        if (err != ESP_OK)
        {
            ESP_LOGW(TAG, "FT5x06 init failed: %s", esp_err_to_name(err));
            return false;
        }
        return true;
    }

    esp_lcd_touch_handle_t handle() const { return touch_; }
    bool ok() const { return touch_ != nullptr; }

private:
    esp_lcd_touch_handle_t touch_ = nullptr;
};
