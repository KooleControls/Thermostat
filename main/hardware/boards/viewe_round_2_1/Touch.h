#pragma once

#include "BoardConfig.h"
#include "drivers/Cst816Touch.h"
#include "driver/i2c_master.h"
#include "esp_log.h"

// ──────────────────────────────────────────────────────────────
// CST826 (CST816 family) capacitive touch for the VIEWE UEDX48480021-MD80ET.
// Owns its own I2C bus (this board has no IO expander). RST/INT are not wired
// to MCU GPIOs (the controller shares the panel reset); we poll. 400 kHz.
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

        Cst816Config cfg{};
        cfg.bus = bus;
        cfg.addr = BoardConfig::TOUCH_I2C_ADDR;
        cfg.x_max = BoardConfig::LCD_H_RES;
        cfg.y_max = BoardConfig::LCD_V_RES;
        cfg.scl_speed_hz = 400000;
        return drv_.Init(cfg);
    }

    esp_lcd_touch_handle_t handle() const { return drv_.handle(); }
    bool ok() const { return drv_.ok(); }

private:
    Cst816Touch drv_;
};
