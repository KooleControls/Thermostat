#pragma once

#include "driver/gpio.h"
#include "esp_lcd_panel_rgb.h"
#include "esp_lcd_panel_ops.h"
#include "esp_log.h"
#include "esp_err.h"
#include <cstddef>

// ──────────────────────────────────────────────────────────────
// Reusable driver — plain 16-bit RGB565 parallel LCD panel via
// esp_lcd_new_rgb_panel (no controller-specific init component). Used by
// panels that need no SPI command sequence (the Sunton 800x480), and by
// boards that push their init separately and then just want the RGB DMA path
// (the VIEWE ST7701, which bit-bangs its own 3-wire init first).
//
// Bounce-buffer mode: a single framebuffer in PSRAM plus small internal-SRAM
// bounce buffers feed the panel's pixel-rate DMA, avoiding the glitching that
// occurs when the LCD streams directly from PSRAM.
//
// The board supplies pins/timings (typically straight from BoardConfig) in an
// RgbPanelConfig and wraps this in its own Display class.
// ──────────────────────────────────────────────────────────────

struct RgbPanelConfig
{
    gpio_num_t de = GPIO_NUM_NC;
    gpio_num_t vsync = GPIO_NUM_NC;
    gpio_num_t hsync = GPIO_NUM_NC;
    gpio_num_t pclk = GPIO_NUM_NC;
    const int *data_pins = nullptr;   // 16 entries (RGB565 data lines)
    int h_res = 0;
    int v_res = 0;
    uint32_t pclk_hz = 0;
    int hsync_pulse_width = 0;
    int hsync_back_porch = 0;
    int hsync_front_porch = 0;
    int vsync_pulse_width = 0;
    int vsync_back_porch = 0;
    int vsync_front_porch = 0;
    bool pclk_active_neg = false;
    bool pclk_idle_high = false;
    lcd_clock_source_t clk_src = LCD_CLK_SRC_DEFAULT;
    size_t dma_burst_size = 0;   // 0 → driver default
};

class RgbPanel
{
    static constexpr const char *TAG = "Display";

public:
    bool Init(const RgbPanelConfig &cfg)
    {
        esp_lcd_rgb_panel_config_t rgb_cfg = {};
        rgb_cfg.clk_src = cfg.clk_src;
        rgb_cfg.data_width = 16;
        rgb_cfg.num_fbs = 1;                                  // single framebuffer in PSRAM
        rgb_cfg.bounce_buffer_size_px = cfg.h_res * 10;       // anti-glitch (10 lines)
        rgb_cfg.dma_burst_size = cfg.dma_burst_size;          // 0 keeps the driver default
        rgb_cfg.hsync_gpio_num = cfg.hsync;
        rgb_cfg.vsync_gpio_num = cfg.vsync;
        rgb_cfg.de_gpio_num = cfg.de;
        rgb_cfg.pclk_gpio_num = cfg.pclk;
        rgb_cfg.disp_gpio_num = GPIO_NUM_NC;
        for (int i = 0; i < 16; ++i)
            rgb_cfg.data_gpio_nums[i] = (gpio_num_t)cfg.data_pins[i];
        rgb_cfg.timings.pclk_hz = cfg.pclk_hz;
        rgb_cfg.timings.h_res = cfg.h_res;
        rgb_cfg.timings.v_res = cfg.v_res;
        rgb_cfg.timings.hsync_pulse_width = cfg.hsync_pulse_width;
        rgb_cfg.timings.hsync_back_porch = cfg.hsync_back_porch;
        rgb_cfg.timings.hsync_front_porch = cfg.hsync_front_porch;
        rgb_cfg.timings.vsync_pulse_width = cfg.vsync_pulse_width;
        rgb_cfg.timings.vsync_back_porch = cfg.vsync_back_porch;
        rgb_cfg.timings.vsync_front_porch = cfg.vsync_front_porch;
        rgb_cfg.timings.flags.pclk_active_neg = cfg.pclk_active_neg;
        rgb_cfg.timings.flags.pclk_idle_high = cfg.pclk_idle_high;
        rgb_cfg.flags.fb_in_psram = true;

        esp_err_t err = esp_lcd_new_rgb_panel(&rgb_cfg, &panel_);
        if (err != ESP_OK)
        {
            ESP_LOGE(TAG, "esp_lcd_new_rgb_panel failed: %s", esp_err_to_name(err));
            return false;
        }
        ESP_ERROR_CHECK(esp_lcd_panel_reset(panel_));
        ESP_ERROR_CHECK(esp_lcd_panel_init(panel_));
        return true;
    }

    esp_lcd_panel_handle_t panel() const { return panel_; }
    bool ok() const { return panel_ != nullptr; }

private:
    esp_lcd_panel_handle_t panel_ = nullptr;
};
