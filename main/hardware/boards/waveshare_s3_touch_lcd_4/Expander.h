#pragma once

#include "BoardConfig.h"
#include "I2cBus.h"
#include "esp_io_expander.h"
#include "custom_io_expander_ch32v003.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// ──────────────────────────────────────────────────────────────
// CH32V003 IO expander @ 0x24 for the Waveshare ESP32-S3-Touch-LCD-4.
//
// This is a Waveshare custom expander (a CH32V003 MCU behind an I2C protocol),
// driven by the waveshare/custom_io_expander_ch32v003 managed component which
// presents the standard esp_io_expander API plus a PWM channel for the panel
// backlight. It owns the LCD reset, touch reset, system power-enable, buzzer,
// and RTC-INT lines (see BoardConfig.h). A lazy singleton (BoardExpander()) is
// shared by Display and Touch.
//
// The power-on / reset sequence and the inverted PWM backlight mapping
// (pwm 0 = full brightness, 255 = off) are transcribed from Waveshare's BSP.
// ──────────────────────────────────────────────────────────────

class Expander
{
    static constexpr const char *TAG = "Expander";

public:
    bool Init()
    {
        if (handle_) return true;
        i2c_master_bus_handle_t bus = BoardI2cBus();
        if (!bus) return false;

        esp_err_t err = custom_io_expander_new_i2c_ch32v003(
            bus, BoardConfig::EXP_I2C_ADDR, &handle_);
        if (err != ESP_OK || !handle_)
        {
            ESP_LOGW(TAG, "CH32V003 expander init failed: %s", esp_err_to_name(err));
            handle_ = nullptr;
            return false;
        }

        esp_io_expander_set_dir(handle_,
            BoardConfig::EXP_SYS_EN | BoardConfig::EXP_BEE_EN |
            BoardConfig::EXP_LCD_RST | BoardConfig::EXP_TOUCH_RST,
            IO_EXPANDER_OUTPUT);
        esp_io_expander_set_dir(handle_, BoardConfig::EXP_RTC_INT, IO_EXPANDER_INPUT);
        return true;
    }

    // Power up the panel and pulse the LCD + touch resets (active-low), per the
    // BSP sequence. Call once before initializing the panel/touch.
    void PowerOnReset()
    {
        if (!handle_) return;
        // Buzzer off, LCD + touch held in reset.
        esp_io_expander_set_level(handle_,
            BoardConfig::EXP_BEE_EN | BoardConfig::EXP_LCD_RST | BoardConfig::EXP_TOUCH_RST, 0);
        vTaskDelay(pdMS_TO_TICKS(200));
        // System power on, resets released.
        esp_io_expander_set_level(handle_,
            BoardConfig::EXP_SYS_EN | BoardConfig::EXP_LCD_RST | BoardConfig::EXP_TOUCH_RST, 1);
        vTaskDelay(pdMS_TO_TICKS(200));
    }

    // Backlight is a PWM channel on the expander; the mapping is inverted
    // (0 = full brightness, 255 = off), matching the BSP.
    void Backlight(bool on) { SetBrightness(on ? 100 : 0); }

    void SetBrightness(int percent)
    {
        if (!handle_) return;
        if (percent < 0)   percent = 0;
        if (percent > 100) percent = 100;
        uint8_t value = (uint8_t)((100 - percent) * 0xFF / 100);
        custom_io_expander_set_pwm(handle_, value);
    }

    esp_io_expander_handle_t handle() const { return handle_; }
    bool ok() const { return handle_ != nullptr; }

private:
    esp_io_expander_handle_t handle_ = nullptr;
};

// Shared instance — initialized on first use by whichever driver comes first.
inline Expander &BoardExpander()
{
    static Expander exp;
    static bool inited = false;
    if (!inited) { exp.Init(); inited = true; }
    return exp;
}
