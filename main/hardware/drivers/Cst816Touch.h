#pragma once

#include "driver/i2c_master.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_touch_cst816s.h"
#include "esp_log.h"
#include "esp_err.h"
#include <cstdint>

// ──────────────────────────────────────────────────────────────
// Reusable driver — CST816 family (incl. CST826) capacitive touch over I2C.
//
// Bus-agnostic: the board supplies the i2c_master bus handle, the controller
// address and resolution. CST816 uses 8-bit register addressing. The board
// wraps this in its own `Touch` class — see a board's Touch.h.
//
// Init() is best-effort: returns false (display keeps working) if the
// controller can't be reached.
// ──────────────────────────────────────────────────────────────

struct Cst816Config
{
    i2c_master_bus_handle_t bus = nullptr;
    uint32_t addr = 0;
    int x_max = 0;
    int y_max = 0;
    gpio_num_t rst = GPIO_NUM_NC;
    gpio_num_t intr = GPIO_NUM_NC;
    uint32_t scl_speed_hz = 400000;
};

class Cst816Touch
{
    static constexpr const char *TAG = "Touch";

public:
    bool Init(const Cst816Config &cfg)
    {
        if (!cfg.bus) return false;

        esp_lcd_panel_io_handle_t tp_io = nullptr;
        esp_lcd_panel_io_i2c_config_t tp_io_cfg = {};
        tp_io_cfg.dev_addr = cfg.addr;
        tp_io_cfg.scl_speed_hz = cfg.scl_speed_hz;
        tp_io_cfg.control_phase_bytes = 1;
        tp_io_cfg.dc_bit_offset = 0;
        tp_io_cfg.lcd_cmd_bits = 8;    // CST816 family uses 8-bit register addrs
        tp_io_cfg.lcd_param_bits = 0;
        tp_io_cfg.flags.disable_control_phase = 1;
        esp_err_t err = esp_lcd_new_panel_io_i2c(cfg.bus, &tp_io_cfg, &tp_io);
        if (err != ESP_OK)
        {
            ESP_LOGW(TAG, "Touch panel IO init failed: %s", esp_err_to_name(err));
            return false;
        }

        esp_lcd_touch_config_t tp_cfg = {};
        tp_cfg.x_max = cfg.x_max;
        tp_cfg.y_max = cfg.y_max;
        tp_cfg.rst_gpio_num = cfg.rst;
        tp_cfg.int_gpio_num = cfg.intr;

        err = esp_lcd_touch_new_i2c_cst816s(tp_io, &tp_cfg, &touch_);
        if (err != ESP_OK)
        {
            ESP_LOGW(TAG, "CST816/CST826 init failed: %s", esp_err_to_name(err));
            return false;
        }
        return true;
    }

    esp_lcd_touch_handle_t handle() const { return touch_; }
    bool ok() const { return touch_ != nullptr; }

private:
    esp_lcd_touch_handle_t touch_ = nullptr;
};
