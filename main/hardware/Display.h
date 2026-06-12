#pragma once

#include "BoardConfig.h"
#include "driver/gpio.h"
#include "esp_lcd_panel_rgb.h"
#include "esp_lcd_panel_ops.h"
#include "esp_log.h"
#include "esp_err.h"

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

    esp_lcd_panel_handle_t panel() const { return panel_; }

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
        esp_lcd_rgb_panel_config_t cfg = {};
        cfg.clk_src = LCD_CLK_SRC_DEFAULT;
        cfg.data_width = 16;
        cfg.num_fbs = 1;                                       // single framebuffer in PSRAM
        cfg.bounce_buffer_size_px = BoardConfig::LCD_H_RES * 10;  // anti-glitch (10 lines)
        cfg.dma_burst_size = 64;
        cfg.hsync_gpio_num = (gpio_num_t)BoardConfig::LCD_PIN_HSYNC;
        cfg.vsync_gpio_num = (gpio_num_t)BoardConfig::LCD_PIN_VSYNC;
        cfg.de_gpio_num = (gpio_num_t)BoardConfig::LCD_PIN_DE;
        cfg.pclk_gpio_num = (gpio_num_t)BoardConfig::LCD_PIN_PCLK;
        cfg.disp_gpio_num = GPIO_NUM_NC;
        for (int i = 0; i < 16; ++i)
            cfg.data_gpio_nums[i] = (gpio_num_t)BoardConfig::LCD_DATA_PINS[i];

        cfg.timings.pclk_hz = BoardConfig::LCD_PIXEL_CLOCK_HZ;
        cfg.timings.h_res = BoardConfig::LCD_H_RES;
        cfg.timings.v_res = BoardConfig::LCD_V_RES;
        cfg.timings.hsync_pulse_width = BoardConfig::LCD_HSYNC_PULSE_WIDTH;
        cfg.timings.hsync_back_porch = BoardConfig::LCD_HSYNC_BACK_PORCH;
        cfg.timings.hsync_front_porch = BoardConfig::LCD_HSYNC_FRONT_PORCH;
        cfg.timings.vsync_pulse_width = BoardConfig::LCD_VSYNC_PULSE_WIDTH;
        cfg.timings.vsync_back_porch = BoardConfig::LCD_VSYNC_BACK_PORCH;
        cfg.timings.vsync_front_porch = BoardConfig::LCD_VSYNC_FRONT_PORCH;
        cfg.timings.flags.pclk_active_neg = BoardConfig::LCD_PCLK_ACTIVE_NEG;
        cfg.timings.flags.pclk_idle_high = BoardConfig::LCD_PCLK_IDLE_HIGH;

        cfg.flags.fb_in_psram = true;

        esp_err_t err = esp_lcd_new_rgb_panel(&cfg, &panel_);
        if (err != ESP_OK)
        {
            ESP_LOGE(TAG, "esp_lcd_new_rgb_panel failed: %s", esp_err_to_name(err));
            return false;
        }
        ESP_ERROR_CHECK(esp_lcd_panel_reset(panel_));
        ESP_ERROR_CHECK(esp_lcd_panel_init(panel_));
        return true;
    }

    esp_lcd_panel_handle_t panel_ = nullptr;
};
