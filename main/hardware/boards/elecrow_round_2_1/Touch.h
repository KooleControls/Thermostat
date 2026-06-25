#pragma once

#include "BoardConfig.h"
#include "I2cBus.h"
#include "Pcf8574.h"
#include "drivers/Cst816Touch.h"

// ──────────────────────────────────────────────────────────────
// CST826 (CST816 family) capacitive touch for the Elecrow CrowPanel 2.1".
// Shares the I2C bus with the PCF8574 expander, which owns the touch
// reset/INT lines — pulsed here before probing. 100 kHz.
// ──────────────────────────────────────────────────────────────

class Touch
{
public:
    bool Init()
    {
        if (!BoardI2cBus()) return false;
        if (BoardPcf().ok())
            BoardPcf().ResetTouch();

        Cst816Config cfg{};
        cfg.bus = BoardI2cBus();
        cfg.addr = BoardConfig::TOUCH_I2C_ADDR;
        cfg.x_max = BoardConfig::LCD_H_RES;
        cfg.y_max = BoardConfig::LCD_V_RES;
        cfg.scl_speed_hz = 100000;
        return drv_.Init(cfg);  // reset/INT on the PCF8574 → NC
    }

    esp_lcd_touch_handle_t handle() const { return drv_.handle(); }
    bool ok() const { return drv_.ok(); }

private:
    Cst816Touch drv_;
};
