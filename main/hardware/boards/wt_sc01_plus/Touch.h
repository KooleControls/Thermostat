#pragma once

#include "BoardConfig.h"
#include "drivers/Ft5x06Touch.h"
#include "driver/i2c_master.h"
#include "esp_log.h"

// ──────────────────────────────────────────────────────────────
// FT6336U (FocalTech FT5x06-compatible, I2C 0x38) capacitive touch for the
// WT-SC01 Plus. Owns its own I2C bus. Native portrait extents with
// swap_xy + mirror_x to line touch up with the landscape display — mirrors the
// proven WT32-SC01 sibling-project config.
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

        i2c_master_bus_handle_t bus = nullptr;
        if (i2c_new_master_bus(&bus_cfg, &bus) != ESP_OK)
        {
            ESP_LOGW(TAG, "I2C bus init failed");
            return false;
        }

        Ft5x06Config cfg{};
        cfg.bus = bus;
        cfg.x_max = BoardConfig::LCD_V_RES;  // native portrait width  (320)
        cfg.y_max = BoardConfig::LCD_H_RES;  // native portrait height (480)
        cfg.rst = (gpio_num_t)BoardConfig::TOUCH_PIN_RST;
        cfg.intr = (gpio_num_t)BoardConfig::TOUCH_PIN_INT;
        cfg.scl_speed_hz = 400000;
        cfg.swap_xy = true;
        cfg.mirror_x = true;
        return drv_.Init(cfg);
    }

    esp_lcd_touch_handle_t handle() const { return drv_.handle(); }
    bool ok() const { return drv_.ok(); }

private:
    Ft5x06Touch drv_;
};
