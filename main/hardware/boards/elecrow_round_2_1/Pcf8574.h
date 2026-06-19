#pragma once

#include "BoardConfig.h"
#include "I2cBus.h"
#include "driver/i2c_master.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// ──────────────────────────────────────────────────────────────
// PCF8574 8-bit I2C IO expander @ 0x21.
//
// The PCF8574 is a transparent quasi-bidirectional latch: a single byte sets
// all 8 pins, and a pin reads as an input only when its latch bit is high
// (write 1, then read). We keep a shadow byte so individual pins can be driven
// without disturbing the others.
//
// On this board the expander owns: LCD power, LCD reset, touch reset/INT, and
// the encoder push-button (see BoardConfig.h). A lazy singleton (BoardPcf())
// is shared by Display/Touch/Knob.
// ──────────────────────────────────────────────────────────────

class Pcf8574
{
    static constexpr const char *TAG = "Pcf8574";

public:
    bool Init()
    {
        if (dev_) return true;
        i2c_master_bus_handle_t bus = BoardI2cBus();
        if (!bus) return false;

        i2c_device_config_t dev_cfg = {};
        dev_cfg.dev_addr_length = I2C_ADDR_BIT_LEN_7;
        dev_cfg.device_address = BoardConfig::PCF8574_I2C_ADDR;
        dev_cfg.scl_speed_hz = 100000;
        if (i2c_master_bus_add_device(bus, &dev_cfg, &dev_) != ESP_OK)
        {
            ESP_LOGW(TAG, "add_device failed");
            dev_ = nullptr;
            return false;
        }
        // Idle state: inputs (button) latched high, outputs to a safe default.
        shadow_ = 0xFF;
        return Write();
    }

    // Drive an output pin; keeps the shadow byte for the other pins.
    bool SetPin(int pin, bool high)
    {
        if (high) shadow_ |= (1u << pin);
        else      shadow_ &= ~(1u << pin);
        return Write();
    }

    // Read a pin as input (its latch bit must be high — true by default here).
    bool GetPin(int pin)
    {
        uint8_t in = 0;
        if (i2c_master_receive(dev_, &in, 1, 100) != ESP_OK) return true;
        return (in >> pin) & 0x1;
    }

    // Power up + hardware-reset the LCD (reset is active-low, pulsed).
    void ResetLcd()
    {
        SetPin(BoardConfig::PCF_LCD_POWER, true);
        SetPin(BoardConfig::PCF_LCD_RST, true);
        vTaskDelay(pdMS_TO_TICKS(20));
        SetPin(BoardConfig::PCF_LCD_RST, false);
        vTaskDelay(pdMS_TO_TICKS(120));
        SetPin(BoardConfig::PCF_LCD_RST, true);
        vTaskDelay(pdMS_TO_TICKS(120));
    }

    // Hardware-reset the touch controller (active-low pulse) and release INT.
    void ResetTouch()
    {
        SetPin(BoardConfig::PCF_TOUCH_INT, true);
        SetPin(BoardConfig::PCF_TOUCH_RST, true);
        vTaskDelay(pdMS_TO_TICKS(10));
        SetPin(BoardConfig::PCF_TOUCH_RST, false);
        vTaskDelay(pdMS_TO_TICKS(20));
        SetPin(BoardConfig::PCF_TOUCH_RST, true);
        vTaskDelay(pdMS_TO_TICKS(50));
    }

    bool ok() const { return dev_ != nullptr; }

private:
    bool Write()
    {
        if (!dev_) return false;
        return i2c_master_transmit(dev_, &shadow_, 1, 100) == ESP_OK;
    }

    i2c_master_dev_handle_t dev_ = nullptr;
    uint8_t shadow_ = 0xFF;
};

// Shared instance — initialized on first use by whichever driver comes first.
inline Pcf8574 &BoardPcf()
{
    static Pcf8574 pcf;
    static bool inited = false;
    if (!inited) { pcf.Init(); inited = true; }
    return pcf;
}
