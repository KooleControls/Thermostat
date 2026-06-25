#pragma once

#include "BoardConfig.h"
#include "I2cBus.h"
#include "Expander.h"
#include "drivers/Gt911Touch.h"

// ──────────────────────────────────────────────────────────────
// GT911 capacitive touch for the Waveshare ESP32-S3-Touch-LCD-4. Shares the
// I2C bus with the CH32V003 expander, whose reset line is pulsed by
// Display::Init() (run before InitTouch()). Address auto-probed (0x5D/0x14);
// RST is on the expander and INT is not wired to a GPIO.
// ──────────────────────────────────────────────────────────────

class Touch
{
public:
    bool Init()
    {
        if (!BoardI2cBus()) return false;
        BoardExpander();  // owns the touch reset line, pulsed in Display::Init()

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
