#pragma once

#include "BoardConfig.h"
#include "drivers/St7796Panel.h"
#include "driver/gpio.h"
#include "esp_log.h"

// ──────────────────────────────────────────────────────────────
// ST7796 LCD panel driver for the WT-SC01 Plus.
//
// 320x480 panel driven over an 8-bit i80/8080 parallel bus. Unlike the RGB
// boards, this is a command-driven controller: pixels are pushed with
// esp_lcd_panel_draw_bitmap, so DisplayManager registers it via the regular
// (non-RGB) LVGL path and needs both the panel handle AND the panel-IO handle
// (the IO handle is where lvgl_port hooks the "flush done" callback). Hence the
// extra io() accessor compared with the RGB boards.
//
// ⚠ Bring-up notes (untested on hardware):
//   • Orientation: native portrait 320x480. If the image is rotated/mirrored,
//     adjust the mirror/swap_xy calls below (MADCTL).
//   • Colour order: ST7796 modules vary RGB vs BGR — flip rgb_ele_order if red
//     and blue are swapped.
//   • If colours look byte-swapped, toggle swap_bytes in DisplayManager's
//     non-RGB branch (this board sets it true to match the i80 byte order).
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
    esp_lcd_panel_io_handle_t io() const { return drv_.io(); }

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
        St7796Config cfg{};
        cfg.dc = (gpio_num_t)BoardConfig::LCD_PIN_DC;
        cfg.wr = (gpio_num_t)BoardConfig::LCD_PIN_WR;
        cfg.cs = (gpio_num_t)BoardConfig::LCD_PIN_CS;
        cfg.data_pins = BoardConfig::LCD_DATA_PINS;
        cfg.pclk_hz = BoardConfig::LCD_PIXEL_CLOCK_HZ;
        // Max bytes per transfer: one flush buffer (lines * width * bytes/px).
        cfg.max_transfer_bytes =
            BoardConfig::LCD_DRAW_BUFFER_LINES * BoardConfig::LCD_H_RES * 2;
        cfg.dma_burst_size = 64;
        cfg.reset_gpio = (gpio_num_t)BoardConfig::LCD_PIN_RST;
        cfg.rgb_ele_order = LCD_RGB_ELEMENT_ORDER_BGR;  // most ST7796 modules are BGR
        cfg.bits_per_pixel = BoardConfig::LCD_BITS_PER_PIXEL;
        cfg.invert_color = true;  // ST7796 typically needs inversion on
        // Native portrait orientation; adjust during bring-up if needed.
        return drv_.Init(cfg);
    }

    St7796Panel drv_;
};
