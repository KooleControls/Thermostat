#pragma once

#include "BoardConfig.h"
#include "Expander.h"
#include "driver/gpio.h"
#include "esp_lcd_panel_rgb.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_st7701.h"
#include "esp_lcd_panel_io_additions.h"
#include "esp_log.h"
#include "esp_err.h"

// ST7701 init sequence for this exact panel, transcribed verbatim from
// Waveshare's ESP-IDF BSP (bsp/esp32_s3_touch_lcd_4, board v3.0.0). The four
// 0xFF writes are CMD2 bank-selects (BK0/BK1/CMD1) and must stay in order.
// Ends with display-inversion-on (0x21) + display-on (0x29).
static const st7701_lcd_init_cmd_t WAVESHARE_ST7701_INIT[] = {
    {0x11, (uint8_t[]){0x00}, 0, 120},   // sleep out

    // CMD2 BK0
    {0xFF, (uint8_t[]){0x77, 0x01, 0x00, 0x00, 0x10}, 5, 0},
    {0xC0, (uint8_t[]){0x3B, 0x00}, 2, 0},
    {0xC1, (uint8_t[]){0x0D, 0x02}, 2, 0},
    {0xC2, (uint8_t[]){0x21, 0x08}, 2, 0},
    {0xCD, (uint8_t[]){0x08}, 1, 0},
    {0xB0, (uint8_t[]){0x00, 0x11, 0x18, 0x0E, 0x11, 0x06, 0x07, 0x08, 0x07, 0x22, 0x04, 0x12, 0x0F, 0xAA, 0x31, 0x18}, 16, 0},
    {0xB1, (uint8_t[]){0x00, 0x11, 0x19, 0x0E, 0x12, 0x07, 0x08, 0x08, 0x08, 0x22, 0x04, 0x11, 0x11, 0xA9, 0x32, 0x18}, 16, 0},

    // CMD2 BK1
    {0xFF, (uint8_t[]){0x77, 0x01, 0x00, 0x00, 0x11}, 5, 0},
    {0xB0, (uint8_t[]){0x60}, 1, 0},
    {0xB1, (uint8_t[]){0x30}, 1, 0},
    {0xB2, (uint8_t[]){0x87}, 1, 0},
    {0xB3, (uint8_t[]){0x80}, 1, 0},
    {0xB5, (uint8_t[]){0x49}, 1, 0},
    {0xB7, (uint8_t[]){0x85}, 1, 0},
    {0xB8, (uint8_t[]){0x21}, 1, 0},
    {0xC1, (uint8_t[]){0x78}, 1, 0},
    {0xC2, (uint8_t[]){0x78}, 1, 20},
    {0xE0, (uint8_t[]){0x00, 0x1B, 0x02}, 3, 0},
    {0xE1, (uint8_t[]){0x08, 0xA0, 0x00, 0x00, 0x07, 0xA0, 0x00, 0x00, 0x00, 0x44, 0x44}, 11, 0},
    {0xE2, (uint8_t[]){0x11, 0x11, 0x44, 0x44, 0xED, 0xA0, 0x00, 0x00, 0xEC, 0xA0, 0x00, 0x00}, 12, 0},
    {0xE3, (uint8_t[]){0x00, 0x00, 0x11, 0x11}, 4, 0},
    {0xE4, (uint8_t[]){0x44, 0x44}, 2, 0},
    {0xE5, (uint8_t[]){0x0A, 0xE9, 0xD8, 0xA0, 0x0C, 0xEB, 0xD8, 0xA0, 0x0E, 0xED, 0xD8, 0xA0, 0x10, 0xEF, 0xD8, 0xA0}, 16, 0},
    {0xE6, (uint8_t[]){0x00, 0x00, 0x11, 0x11}, 4, 0},
    {0xE7, (uint8_t[]){0x44, 0x44}, 2, 0},
    {0xE8, (uint8_t[]){0x09, 0xE8, 0xD8, 0xA0, 0x0B, 0xEA, 0xD8, 0xA0, 0x0D, 0xEC, 0xD8, 0xA0, 0x0F, 0xEE, 0xD8, 0xA0}, 16, 0},
    {0xEB, (uint8_t[]){0x02, 0x00, 0xE4, 0xE4, 0x88, 0x00, 0x40}, 7, 0},
    {0xEC, (uint8_t[]){0x3C, 0x00}, 2, 0},
    {0xED, (uint8_t[]){0xAB, 0x89, 0x76, 0x54, 0x02, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x20, 0x45, 0x67, 0x98, 0xBA}, 16, 0},

    // Back to CMD1 (user command set)
    {0xFF, (uint8_t[]){0x77, 0x01, 0x00, 0x00, 0x00}, 5, 0},
    {0x36, (uint8_t[]){0x00}, 1, 0},     // MADCTL — flip here if orientation wrong
    {0x3A, (uint8_t[]){0x66}, 1, 0},     // COLMOD = RGB666

    {0x21, (uint8_t[]){0x00}, 0, 120},   // display inversion on
    {0x29, (uint8_t[]){0x00}, 0, 0},     // display on
};

// ──────────────────────────────────────────────────────────────
// ST7701 480x480 RGB panel for the Waveshare ESP32-S3-Touch-LCD-4.
//
// The ST7701 is configured over a 3-wire SPI init sequence (CS/SCK/SDA on
// direct GPIOs) and then streams pixels over a 16-bit RGB565 parallel bus
// (same RGB DMA path as the other RGB boards). LCD power-enable and hardware
// reset are on the CH32V003 expander, and the backlight is the expander's PWM
// channel — so the panel's own reset_gpio is NC and we run the expander's
// power-on/reset sequence before init.
//
// ⚠ Bring-up checklist:
//   • RGB vs BGR element order (start with RGB, per the BSP).
//   • Orientation — adjust MADCTL (0x36) in the init array above if mirrored.
//   The pin map / init sequence / timing are straight from Waveshare's BSP, so
//   this should come up correctly without per-pixel probing.
// ──────────────────────────────────────────────────────────────

class Display
{
    static constexpr const char *TAG = "Display";

public:
    bool Init()
    {
        if (!BoardExpander().ok())
        {
            ESP_LOGE(TAG, "CH32V003 expander unavailable — cannot power LCD");
            return false;
        }
        // Power the panel and pulse LCD/touch reset through the expander.
        BoardExpander().PowerOnReset();
        BoardExpander().Backlight(false);  // dark until the first frame is rendered

        return InitPanel();
    }

    esp_lcd_panel_handle_t panel() const { return panel_; }

    void Backlight(bool on) { BoardExpander().Backlight(on); }

    static constexpr int Width()  { return BoardConfig::LCD_H_RES; }
    static constexpr int Height() { return BoardConfig::LCD_V_RES; }

private:
    bool InitPanel()
    {
        // 3-wire SPI panel IO for the ST7701 init sequence (all direct GPIO).
        // Built explicitly rather than via ST7701_PANEL_IO_3WIRE_SPI_CONFIG()
        // because that macro's C designated-initializer order doesn't hold in
        // C++ (same caveat as the other board drivers).
        spi_line_config_t line_config = {};
        line_config.cs_io_type = IO_TYPE_GPIO;
        line_config.cs_gpio_num = (gpio_num_t)BoardConfig::LCD_SPI_CS;
        line_config.scl_io_type = IO_TYPE_GPIO;
        line_config.scl_gpio_num = (gpio_num_t)BoardConfig::LCD_SPI_SCK;
        line_config.sda_io_type = IO_TYPE_GPIO;
        line_config.sda_gpio_num = (gpio_num_t)BoardConfig::LCD_SPI_SDA;
        line_config.io_expander = nullptr;  // CS is a direct GPIO on this board

        esp_lcd_panel_io_3wire_spi_config_t io_config = {};
        io_config.line_config = line_config;
        io_config.expect_clk_speed = PANEL_IO_3WIRE_SPI_CLK_MAX;
        io_config.spi_mode = 0;   // BSP uses MODE0 (scl_active_edge = 0)
        io_config.lcd_cmd_bytes = 1;
        io_config.lcd_param_bytes = 1;
        io_config.flags.use_dc_bit = 1;
        io_config.flags.del_keep_cs_inactive = 1;

        esp_lcd_panel_io_handle_t io_handle = nullptr;
        esp_err_t err = esp_lcd_new_panel_io_3wire_spi(&io_config, &io_handle);
        if (err != ESP_OK)
        {
            ESP_LOGE(TAG, "3-wire SPI IO init failed: %s", esp_err_to_name(err));
            return false;
        }

        // RGB data bus config (consumed by the ST7701 driver via vendor_config).
        esp_lcd_rgb_panel_config_t rgb_cfg = {};
        rgb_cfg.clk_src = LCD_CLK_SRC_DEFAULT;
        rgb_cfg.data_width = 16;
        rgb_cfg.num_fbs = 1;
        rgb_cfg.bounce_buffer_size_px = BoardConfig::LCD_H_RES * 10;
        rgb_cfg.dma_burst_size = 64;
        rgb_cfg.hsync_gpio_num = (gpio_num_t)BoardConfig::LCD_PIN_HSYNC;
        rgb_cfg.vsync_gpio_num = (gpio_num_t)BoardConfig::LCD_PIN_VSYNC;
        rgb_cfg.de_gpio_num = (gpio_num_t)BoardConfig::LCD_PIN_DE;
        rgb_cfg.pclk_gpio_num = (gpio_num_t)BoardConfig::LCD_PIN_PCLK;
        rgb_cfg.disp_gpio_num = GPIO_NUM_NC;
        for (int i = 0; i < 16; ++i)
            rgb_cfg.data_gpio_nums[i] = (gpio_num_t)BoardConfig::LCD_DATA_PINS[i];
        rgb_cfg.timings.pclk_hz = BoardConfig::LCD_PIXEL_CLOCK_HZ;
        rgb_cfg.timings.h_res = BoardConfig::LCD_H_RES;
        rgb_cfg.timings.v_res = BoardConfig::LCD_V_RES;
        rgb_cfg.timings.hsync_pulse_width = BoardConfig::LCD_HSYNC_PULSE_WIDTH;
        rgb_cfg.timings.hsync_back_porch = BoardConfig::LCD_HSYNC_BACK_PORCH;
        rgb_cfg.timings.hsync_front_porch = BoardConfig::LCD_HSYNC_FRONT_PORCH;
        rgb_cfg.timings.vsync_pulse_width = BoardConfig::LCD_VSYNC_PULSE_WIDTH;
        rgb_cfg.timings.vsync_back_porch = BoardConfig::LCD_VSYNC_BACK_PORCH;
        rgb_cfg.timings.vsync_front_porch = BoardConfig::LCD_VSYNC_FRONT_PORCH;
        rgb_cfg.timings.flags.pclk_active_neg = BoardConfig::LCD_PCLK_ACTIVE_NEG;
        rgb_cfg.timings.flags.pclk_idle_high = BoardConfig::LCD_PCLK_IDLE_HIGH;
        rgb_cfg.flags.fb_in_psram = true;

        st7701_vendor_config_t vendor_cfg = {};
        vendor_cfg.rgb_config = &rgb_cfg;
        vendor_cfg.init_cmds = WAVESHARE_ST7701_INIT;
        vendor_cfg.init_cmds_size = sizeof(WAVESHARE_ST7701_INIT) / sizeof(WAVESHARE_ST7701_INIT[0]);
        vendor_cfg.flags.auto_del_panel_io = 0;
        vendor_cfg.flags.mirror_by_cmd = 1;  // matches the BSP

        esp_lcd_panel_dev_config_t panel_cfg = {};
        panel_cfg.reset_gpio_num = GPIO_NUM_NC;  // reset done via the expander above
        panel_cfg.rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB;
        panel_cfg.bits_per_pixel = 16;
        panel_cfg.vendor_config = &vendor_cfg;

        err = esp_lcd_new_panel_st7701(io_handle, &panel_cfg, &panel_);
        if (err != ESP_OK)
        {
            ESP_LOGE(TAG, "esp_lcd_new_panel_st7701 failed: %s", esp_err_to_name(err));
            return false;
        }
        ESP_ERROR_CHECK(esp_lcd_panel_reset(panel_));
        ESP_ERROR_CHECK(esp_lcd_panel_init(panel_));
        return true;
    }

    esp_lcd_panel_handle_t panel_ = nullptr;
};
