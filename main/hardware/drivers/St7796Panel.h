#pragma once

#include "driver/gpio.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_st7796.h"
#include "esp_log.h"
#include "esp_err.h"
#include <cstddef>

// ──────────────────────────────────────────────────────────────
// Reusable driver — Sitronix ST7796 LCD over an 8-bit i80/8080 parallel bus.
//
// Unlike the RGB panels this is a command-driven controller: pixels are pushed
// with esp_lcd_panel_draw_bitmap, so DisplayManager registers it via the
// regular (non-RGB) LVGL path and needs BOTH the panel handle and the panel-IO
// handle (lvgl_port hooks its "flush done" callback on the IO handle). Hence
// the io() accessor alongside panel().
//
// The board supplies the bus pins, resolution-derived transfer size, reset pin
// and colour/inversion flags in an St7796Config and wraps this in its own
// Display class.
// ──────────────────────────────────────────────────────────────

struct St7796Config
{
    // i80 (8080) parallel bus
    gpio_num_t dc = GPIO_NUM_NC;
    gpio_num_t wr = GPIO_NUM_NC;
    gpio_num_t cs = GPIO_NUM_NC;
    const int *data_pins = nullptr;   // 8 entries
    uint32_t pclk_hz = 0;
    size_t max_transfer_bytes = 0;    // one flush buffer: lines * width * bytes/px
    size_t dma_burst_size = 64;

    // ST7796 panel device
    gpio_num_t reset_gpio = GPIO_NUM_NC;
    lcd_rgb_element_order_t rgb_ele_order = LCD_RGB_ELEMENT_ORDER_BGR;  // most modules are BGR
    int bits_per_pixel = 16;
    bool invert_color = true;   // ST7796 typically needs inversion on
};

class St7796Panel
{
    static constexpr const char *TAG = "Display";

public:
    bool Init(const St7796Config &cfg)
    {
        // ── i80 (8080) parallel bus ────────────────────────────────
        esp_lcd_i80_bus_config_t bus_cfg = {};
        bus_cfg.clk_src = LCD_CLK_SRC_DEFAULT;
        bus_cfg.dc_gpio_num = cfg.dc;
        bus_cfg.wr_gpio_num = cfg.wr;
        bus_cfg.bus_width = 8;
        for (int i = 0; i < 8; ++i)
            bus_cfg.data_gpio_nums[i] = (gpio_num_t)cfg.data_pins[i];
        bus_cfg.max_transfer_bytes = cfg.max_transfer_bytes;
        bus_cfg.dma_burst_size = cfg.dma_burst_size;

        esp_lcd_i80_bus_handle_t i80_bus = nullptr;
        esp_err_t err = esp_lcd_new_i80_bus(&bus_cfg, &i80_bus);
        if (err != ESP_OK)
        {
            ESP_LOGE(TAG, "esp_lcd_new_i80_bus failed: %s", esp_err_to_name(err));
            return false;
        }

        esp_lcd_panel_io_i80_config_t io_cfg = {};
        io_cfg.cs_gpio_num = cfg.cs;
        io_cfg.pclk_hz = cfg.pclk_hz;
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
        panel_cfg.reset_gpio_num = cfg.reset_gpio;
        panel_cfg.rgb_ele_order = cfg.rgb_ele_order;
        panel_cfg.bits_per_pixel = cfg.bits_per_pixel;

        err = esp_lcd_new_panel_st7796(io_, &panel_cfg, &panel_);
        if (err != ESP_OK)
        {
            ESP_LOGE(TAG, "esp_lcd_new_panel_st7796 failed: %s", esp_err_to_name(err));
            return false;
        }

        ESP_ERROR_CHECK(esp_lcd_panel_reset(panel_));
        ESP_ERROR_CHECK(esp_lcd_panel_init(panel_));
        if (cfg.invert_color)
            ESP_ERROR_CHECK(esp_lcd_panel_invert_color(panel_, true));
        ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel_, true));
        return true;
    }

    esp_lcd_panel_handle_t panel() const { return panel_; }
    esp_lcd_panel_io_handle_t io() const { return io_; }
    bool ok() const { return panel_ != nullptr; }

private:
    esp_lcd_panel_io_handle_t io_ = nullptr;
    esp_lcd_panel_handle_t panel_ = nullptr;
};
