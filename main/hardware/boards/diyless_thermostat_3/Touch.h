#pragma once

#include "BoardConfig.h"
#include "I2cBus.h"
#include "drivers/Gt911Touch.h"

// ──────────────────────────────────────────────────────────────
// GT911 capacitive touch for the DIYLESS Thermostat 3. Shares the I2C bus
// (SDA17/SCL18) with the AHT20. No ESP-controlled reset; INT (GPIO10) is left
// unused — we poll (esp_lvgl_port). Address auto-probed (this unit reports 0x14).
// ──────────────────────────────────────────────────────────────

class Touch
{
public:
    bool Init()
    {
        if (!BoardI2cBus()) return false;

        Gt911Config cfg{};
        cfg.bus = BoardI2cBus();
        cfg.x_max = BoardConfig::LCD_H_RES;
        cfg.y_max = BoardConfig::LCD_V_RES;
        return drv_.Init(cfg);  // auto-probe, NC rst/int, 400 kHz
    }

    esp_lcd_touch_handle_t handle() const { return drv_.handle(); }
    bool ok() const { return drv_.ok(); }

private:
    Gt911Touch drv_;
};
