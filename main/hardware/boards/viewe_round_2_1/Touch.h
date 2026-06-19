#pragma once

#include "BoardConfig.h"
#include "driver/i2c_master.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_touch_cst816s.h"
#include "esp_log.h"
#include "esp_err.h"

// ──────────────────────────────────────────────────────────────
// CST826 capacitive touch (CST816 family) for the VIEWE UEDX48480021-MD80ET.
// Owns its own I2C bus (this board has no IO expander). RST/INT are not wired
// to MCU GPIOs — the controller shares the panel reset, and we poll over I2C.
// Init() is best-effort: returns false (display keeps working) if absent.
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

        esp_lcd_panel_io_handle_t tp_io = nullptr;
        esp_lcd_panel_io_i2c_config_t tp_io_cfg = {};
        tp_io_cfg.dev_addr = BoardConfig::TOUCH_I2C_ADDR;
        tp_io_cfg.scl_speed_hz = 400000;
        tp_io_cfg.control_phase_bytes = 1;
        tp_io_cfg.dc_bit_offset = 0;
        tp_io_cfg.lcd_cmd_bits = 8;    // CST816 family uses 8-bit register addrs
        tp_io_cfg.lcd_param_bits = 0;
        tp_io_cfg.flags.disable_control_phase = 1;

        err = esp_lcd_new_panel_io_i2c(i2c_bus, &tp_io_cfg, &tp_io);
        if (err != ESP_OK)
        {
            ESP_LOGW(TAG, "Touch panel IO init failed: %s", esp_err_to_name(err));
            return false;
        }

        esp_lcd_touch_config_t tp_cfg = {};
        tp_cfg.x_max = BoardConfig::LCD_H_RES;
        tp_cfg.y_max = BoardConfig::LCD_V_RES;
        tp_cfg.rst_gpio_num = GPIO_NUM_NC;  // shares the LCD reset
        tp_cfg.int_gpio_num = GPIO_NUM_NC;  // not wired

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
