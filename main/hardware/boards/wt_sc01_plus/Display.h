#pragma once

#include "BoardConfig.h"
#include "driver/gpio.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_st7796.h"
#include "esp_log.h"
#include "esp_err.h"

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

    esp_lcd_panel_handle_t panel() const { return panel_; }
    esp_lcd_panel_io_handle_t io() const { return io_; }

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
        // ── i80 (8080) parallel bus ────────────────────────────────
        esp_lcd_i80_bus_config_t bus_cfg = {};
        bus_cfg.clk_src = LCD_CLK_SRC_DEFAULT;
        bus_cfg.dc_gpio_num = (gpio_num_t)BoardConfig::LCD_PIN_DC;
        bus_cfg.wr_gpio_num = (gpio_num_t)BoardConfig::LCD_PIN_WR;
        bus_cfg.bus_width = 8;
        for (int i = 0; i < 8; ++i)
            bus_cfg.data_gpio_nums[i] = (gpio_num_t)BoardConfig::LCD_DATA_PINS[i];
        // Max bytes per transfer: one flush buffer (lines * width * bytes/px).
        bus_cfg.max_transfer_bytes =
            BoardConfig::LCD_DRAW_BUFFER_LINES * BoardConfig::LCD_H_RES * 2;
        bus_cfg.dma_burst_size = 64;

        esp_lcd_i80_bus_handle_t i80_bus = nullptr;
        esp_err_t err = esp_lcd_new_i80_bus(&bus_cfg, &i80_bus);
        if (err != ESP_OK)
        {
            ESP_LOGE(TAG, "esp_lcd_new_i80_bus failed: %s", esp_err_to_name(err));
            return false;
        }

        esp_lcd_panel_io_i80_config_t io_cfg = {};
        io_cfg.cs_gpio_num = (gpio_num_t)BoardConfig::LCD_PIN_CS;
        io_cfg.pclk_hz = BoardConfig::LCD_PIXEL_CLOCK_HZ;
        io_cfg.trans_queue_depth = 10;
        io_cfg.dc_levels.dc_idle_level = 0;
        io_cfg.dc_levels.dc_cmd_level = 0;
        io_cfg.dc_levels.dc_dummy_level = 0;
        io_cfg.dc_levels.dc_data_level = 1;
        io_cfg.lcd_cmd_bits = 8;
        io_cfg.lcd_param_bits = 8;

        err = esp_lcd_new_panel_io_i80(i80_bus, &io_cfg, &io_);
        if (err != ESP_OK)
        {
            ESP_LOGE(TAG, "esp_lcd_new_panel_io_i80 failed: %s", esp_err_to_name(err));
            return false;
        }

        // ── ST7796 panel ───────────────────────────────────────────
        esp_lcd_panel_dev_config_t panel_cfg = {};
        panel_cfg.reset_gpio_num = (gpio_num_t)BoardConfig::LCD_PIN_RST;
        panel_cfg.rgb_ele_order = LCD_RGB_ELEMENT_ORDER_BGR;  // most ST7796 modules are BGR
        panel_cfg.bits_per_pixel = BoardConfig::LCD_BITS_PER_PIXEL;

        err = esp_lcd_new_panel_st7796(io_, &panel_cfg, &panel_);
        if (err != ESP_OK)
        {
            ESP_LOGE(TAG, "esp_lcd_new_panel_st7796 failed: %s", esp_err_to_name(err));
            return false;
        }

        ESP_ERROR_CHECK(esp_lcd_panel_reset(panel_));
        ESP_ERROR_CHECK(esp_lcd_panel_init(panel_));
        // Native portrait orientation; adjust during bring-up if needed.
        ESP_ERROR_CHECK(esp_lcd_panel_invert_color(panel_, true));  // ST7796 typically needs inversion on
        ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel_, true));
        return true;
    }

    esp_lcd_panel_io_handle_t io_ = nullptr;
    esp_lcd_panel_handle_t panel_ = nullptr;
};
