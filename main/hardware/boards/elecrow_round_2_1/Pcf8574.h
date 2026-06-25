#pragma once

#include "BoardConfig.h"
#include "I2cBus.h"
#include "drivers/Pcf8574.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// ──────────────────────────────────────────────────────────────
// Board wiring for the Elecrow CrowPanel 2.1"'s PCF8574 IO expander @ 0x21.
//
// Wraps the generic Pcf8574 chip driver (drivers/Pcf8574.h) with this board's
// semantics: the expander owns LCD power, LCD reset, touch reset/INT, and the
// encoder push-button (pin assignments in BoardConfig.h). A lazy singleton
// (BoardPcf()) is shared by Display/Touch/Knob.
// ──────────────────────────────────────────────────────────────

class BoardPcf8574
{
public:
    bool Init()
    {
        // Idle state (0xFF): inputs (button) latched high, outputs to a safe default.
        return dev_.Init(BoardI2cBus(), BoardConfig::PCF8574_I2C_ADDR, 100000, 0xFF);
    }

    bool SetPin(int pin, bool high) { return dev_.SetPin(pin, high); }
    bool GetPin(int pin) { return dev_.GetPin(pin); }
    bool ok() const { return dev_.ok(); }

    // Power up + hardware-reset the LCD (reset is active-low, pulsed).
    void ResetLcd()
    {
        dev_.SetPin(BoardConfig::PCF_LCD_POWER, true);
        dev_.SetPin(BoardConfig::PCF_LCD_RST, true);
        vTaskDelay(pdMS_TO_TICKS(20));
        dev_.SetPin(BoardConfig::PCF_LCD_RST, false);
        vTaskDelay(pdMS_TO_TICKS(120));
        dev_.SetPin(BoardConfig::PCF_LCD_RST, true);
        vTaskDelay(pdMS_TO_TICKS(120));
    }

    // Hardware-reset the touch controller (active-low pulse) and release INT.
    void ResetTouch()
    {
        dev_.SetPin(BoardConfig::PCF_TOUCH_INT, true);
        dev_.SetPin(BoardConfig::PCF_TOUCH_RST, true);
        vTaskDelay(pdMS_TO_TICKS(10));
        dev_.SetPin(BoardConfig::PCF_TOUCH_RST, false);
        vTaskDelay(pdMS_TO_TICKS(20));
        dev_.SetPin(BoardConfig::PCF_TOUCH_RST, true);
        vTaskDelay(pdMS_TO_TICKS(50));
    }

private:
    Pcf8574 dev_;
};

// Shared instance — initialized on first use by whichever driver comes first.
inline BoardPcf8574 &BoardPcf()
{
    static BoardPcf8574 pcf;
    static bool inited = false;
    if (!inited) { pcf.Init(); inited = true; }
    return pcf;
}
