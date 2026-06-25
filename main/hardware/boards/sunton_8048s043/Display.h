#pragma once

#include "BoardConfig.h"
#include "drivers/RgbPanel.h"
#include "driver/gpio.h"
#include "esp_log.h"

// ──────────────────────────────────────────────────────────────
// RGB LCD panel + backlight driver for the ESP32-8048S043.
// 800x480 16-bit RGB565 parallel panel. Pins/timings live in BoardConfig.h.
//
// Uses bounce-buffer mode: a single framebuffer in PSRAM plus small
// internal-SRAM bounce buffers feed the panel's pixel-rate DMA, which avoids
// glitching that occurs when the LCD streams directly from PSRAM.
// ──────────────────────────────────────────────────────────────

class Display
{
    static constexpr const char *TAG = "Display";

public:
    bool Init()
    {
        if (!InitBacklight()) return false;
        return InitPanel();
    }

    esp_lcd_panel_handle_t panel() const { return drv_.panel(); }

    void Backlight(bool on)
    {
        gpio_set_level((gpio_num_t)BoardConfig::LCD_PIN_BACKLIGHT, on ? 1 : 0);
    }

    static constexpr int Width()  { return BoardConfig::LCD_H_RES; }
    static constexpr int Height() { return BoardConfig::LCD_V_RES; }

private:
    bool InitBacklight()
    {
        gpio_config_t bk = {};
        bk.mode = GPIO_MODE_OUTPUT;
        bk.pin_bit_mask = 1ULL << BoardConfig::LCD_PIN_BACKLIGHT;
        if (gpio_config(&bk) != ESP_OK)
        {
            ESP_LOGE(TAG, "Backlight GPIO config failed");
            return false;
        }
        // Keep dark until the first frame is rendered (avoids a flash of noise).
        Backlight(false);
        return true;
    }

    bool InitPanel()
    {
        RgbPanelConfig cfg{};
        cfg.de = (gpio_num_t)BoardConfig::LCD_PIN_DE;
        cfg.vsync = (gpio_num_t)BoardConfig::LCD_PIN_VSYNC;
        cfg.hsync = (gpio_num_t)BoardConfig::LCD_PIN_HSYNC;
        cfg.pclk = (gpio_num_t)BoardConfig::LCD_PIN_PCLK;
        cfg.data_pins = BoardConfig::LCD_DATA_PINS;
        cfg.h_res = BoardConfig::LCD_H_RES;
        cfg.v_res = BoardConfig::LCD_V_RES;
        cfg.pclk_hz = BoardConfig::LCD_PIXEL_CLOCK_HZ;
        cfg.hsync_pulse_width = BoardConfig::LCD_HSYNC_PULSE_WIDTH;
        cfg.hsync_back_porch = BoardConfig::LCD_HSYNC_BACK_PORCH;
        cfg.hsync_front_porch = BoardConfig::LCD_HSYNC_FRONT_PORCH;
        cfg.vsync_pulse_width = BoardConfig::LCD_VSYNC_PULSE_WIDTH;
        cfg.vsync_back_porch = BoardConfig::LCD_VSYNC_BACK_PORCH;
        cfg.vsync_front_porch = BoardConfig::LCD_VSYNC_FRONT_PORCH;
        cfg.pclk_active_neg = BoardConfig::LCD_PCLK_ACTIVE_NEG;
        cfg.pclk_idle_high = BoardConfig::LCD_PCLK_IDLE_HIGH;
        cfg.clk_src = LCD_CLK_SRC_DEFAULT;
        cfg.dma_burst_size = 64;
        return drv_.Init(cfg);
    }

    RgbPanel drv_;
};
