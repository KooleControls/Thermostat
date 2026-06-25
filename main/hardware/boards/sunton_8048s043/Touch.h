#pragma once

#include "BoardConfig.h"
#include "drivers/Gt911Touch.h"
#include "driver/i2c_master.h"
#include "esp_log.h"

// ──────────────────────────────────────────────────────────────
// GT911 capacitive touch for the Sunton ESP32-8048S043. Touch is the only
// device on its I2C bus, so the bus is created here. The GT911 ships
// factory-configured for 480x272 (see BoardConfig.h), so raw coordinates are
// scaled up to the 800x480 panel. Fixed address 0x5D @ 100 kHz, RST/INT wired.
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

        Gt911Config cfg{};
        cfg.bus = bus;
        cfg.x_max = BoardConfig::LCD_H_RES;
        cfg.y_max = BoardConfig::LCD_V_RES;
        cfg.rst = (gpio_num_t)BoardConfig::TOUCH_PIN_RST;
        cfg.intr = (gpio_num_t)BoardConfig::TOUCH_PIN_INT;
        cfg.scl_speed_hz = 100000;
        cfg.auto_probe_addr = false;  // fixed 0x5D on this board
        cfg.process_coordinates = &Touch::ScaleCoordinates;
        return drv_.Init(cfg);
    }

    esp_lcd_touch_handle_t handle() const { return drv_.handle(); }
    bool ok() const { return drv_.ok(); }

private:
    // GT911 raw (480x272 factory config) → 800x480 panel coordinates.
    static void ScaleCoordinates(esp_lcd_touch_handle_t, uint16_t *x, uint16_t *y,
                                 uint16_t *, uint8_t *pointNum, uint8_t maxPointNum)
    {
        for (uint8_t i = 0; i < *pointNum && i < maxPointNum; i++)
        {
            x[i] = (uint16_t)((uint32_t)x[i] * BoardConfig::LCD_H_RES / BoardConfig::TOUCH_RAW_X_MAX);
            y[i] = (uint16_t)((uint32_t)y[i] * BoardConfig::LCD_V_RES / BoardConfig::TOUCH_RAW_Y_MAX);
        }
    }

    Gt911Touch drv_;
};
