#pragma once

#include "BoardConfig.h"
#include "Pcf8574.h"
#include "driver/gpio.h"
#include "esp_lcd_panel_rgb.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_st7701.h"
#include "esp_lcd_panel_io_additions.h"
#include "esp_log.h"
#include "esp_err.h"

// ST7701S init sequence for this exact panel, transcribed from Elecrow's
// official ESPHome config (esphome2.1.yaml). Must be static const at file
// scope. The four 0xFF writes are CMD2 bank-selects (BK0/BK1/BK3/CMD1) and
// must stay in order. Ends with sleep-out (0x11) + display-on (0x29).
static const st7701_lcd_init_cmd_t ELECROW_ST7701_INIT[] = {
    {0x01, (uint8_t[]){0x00}, 0, 5},   // software reset

    // CMD2 BK0
    {0xFF, (uint8_t[]){0x77, 0x01, 0x00, 0x00, 0x10}, 5, 0},
    {0xCC, (uint8_t[]){0x10}, 1, 0},
    {0xCD, (uint8_t[]){0x08}, 1, 0},
    {0xB0, (uint8_t[]){0x02, 0x13, 0x1B, 0x0D, 0x10, 0x05, 0x08, 0x07, 0x07, 0x24, 0x04, 0x11, 0x0E, 0x2C, 0x33, 0x1D}, 16, 0},
    {0xB1, (uint8_t[]){0x05, 0x13, 0x1B, 0x0D, 0x11, 0x05, 0x08, 0x07, 0x07, 0x24, 0x04, 0x11, 0x0E, 0x2C, 0x33, 0x1D}, 16, 0},

    // CMD2 BK1
    {0xFF, (uint8_t[]){0x77, 0x01, 0x00, 0x00, 0x11}, 5, 0},
    {0xB0, (uint8_t[]){0x5D}, 1, 0},
    {0xB1, (uint8_t[]){0x43}, 1, 0},
    {0xB2, (uint8_t[]){0x81}, 1, 0},
    {0xB3, (uint8_t[]){0x80}, 1, 0},
    {0xB5, (uint8_t[]){0x43}, 1, 0},
    {0xB7, (uint8_t[]){0x85}, 1, 0},
    {0xB8, (uint8_t[]){0x20}, 1, 0},
    {0xC1, (uint8_t[]){0x78}, 1, 0},
    {0xC2, (uint8_t[]){0x78}, 1, 0},
    {0xD0, (uint8_t[]){0x88}, 1, 0},
    {0xE0, (uint8_t[]){0x00, 0x00, 0x02}, 3, 0},
    {0xE1, (uint8_t[]){0x03, 0xA0, 0x00, 0x00, 0x04, 0xA0, 0x00, 0x00, 0x00, 0x20, 0x20}, 11, 0},
    {0xE2, (uint8_t[]){0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, 13, 0},
    {0xE3, (uint8_t[]){0x00, 0x00, 0x11, 0x00}, 4, 0},
    {0xE4, (uint8_t[]){0x22, 0x00}, 2, 0},
    {0xE5, (uint8_t[]){0x05, 0xEC, 0xA0, 0xA0, 0x07, 0xEE, 0xA0, 0xA0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, 16, 0},
    {0xE6, (uint8_t[]){0x00, 0x00, 0x11, 0x00}, 4, 0},
    {0xE7, (uint8_t[]){0x22, 0x00}, 2, 0},
    {0xE8, (uint8_t[]){0x06, 0xED, 0xA0, 0xA0, 0x08, 0xEF, 0xA0, 0xA0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, 16, 0},
    {0xEB, (uint8_t[]){0x00, 0x00, 0x40, 0x40, 0x00, 0x00, 0x00}, 7, 0},
    {0xED, (uint8_t[]){0xFF, 0xFF, 0xFF, 0xBA, 0x0A, 0xBF, 0x45, 0xFF, 0xFF, 0x54, 0xFB, 0xA0, 0xAB, 0xFF, 0xFF, 0xFF}, 16, 0},
    {0xEF, (uint8_t[]){0x10, 0x0D, 0x04, 0x08, 0x3F, 0x1F}, 6, 0},

    // CMD2 BK3
    {0xFF, (uint8_t[]){0x77, 0x01, 0x00, 0x00, 0x13}, 5, 0},
    {0xEF, (uint8_t[]){0x08}, 1, 0},

    // Back to CMD1 (user command set)
    {0xFF, (uint8_t[]){0x77, 0x01, 0x00, 0x00, 0x00}, 5, 0},
    {0x36, (uint8_t[]){0x00}, 1, 0},   // MADCTL — flip here if orientation wrong
    {0x3A, (uint8_t[]){0x60}, 1, 0},   // COLMOD = RGB666

    {0x11, (uint8_t[]){0x00}, 0, 100}, // sleep out
    {0x29, (uint8_t[]){0x00}, 0, 50},  // display on
};

// ──────────────────────────────────────────────────────────────
// ST7701S 480x480 round RGB panel for the Elecrow CrowPanel 2.1".
//
// The ST7701S is configured over a 3-wire SPI init sequence and then streams
// pixels over a 16-bit RGB565 parallel bus (same RGB DMA path as the Sunton
// board). LCD power-enable and hardware reset are on the PCF8574 expander, so
// the panel's own reset_gpio is -1 and we pulse reset via the expander first.
//
// ⚠ Bring-up checklist (untested on hardware):
//   • RGB vs BGR element order (Elecrow's two reference stacks disagree).
//   • The exact ST7701S init command table — this uses the esp_lcd_st7701
//     built-in default; if the panel shows wrong gamma/orientation, port
//     Elecrow's init array from RotaryScreen_2_1.ino into init_cmds.
//   • HSYNC/VSYNC porches and pixel clock (BoardConfig values are ESPHome's).
// ──────────────────────────────────────────────────────────────

class Display
{
    static constexpr const char *TAG = "Display";

public:
    bool Init()
    {
        if (!InitBacklight()) return false;

        // Power + hardware-reset the panel through the IO expander before init.
        if (!BoardPcf().ok())
        {
            ESP_LOGE(TAG, "PCF8574 unavailable — cannot power LCD");
            return false;
        }
        BoardPcf().ResetLcd();

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
        Backlight(false);  // dark until the first frame is rendered
        return true;
    }

    bool InitPanel()
    {
        // 3-wire SPI panel IO for the ST7701S init sequence.
        spi_line_config_t line_config = {};
        line_config.cs_io_type = IO_TYPE_GPIO;
        line_config.cs_gpio_num = (gpio_num_t)BoardConfig::LCD_SPI_CS;
        line_config.scl_io_type = IO_TYPE_GPIO;
        line_config.scl_gpio_num = (gpio_num_t)BoardConfig::LCD_SPI_SCK;
        line_config.sda_io_type = IO_TYPE_GPIO;
        line_config.sda_gpio_num = (gpio_num_t)BoardConfig::LCD_SPI_SDA;
        line_config.io_expander = nullptr;

        esp_lcd_panel_io_3wire_spi_config_t io_config = {};
        io_config.line_config = line_config;
        io_config.expect_clk_speed = PANEL_IO_3WIRE_SPI_CLK_MAX;
        io_config.spi_mode = 3;   // Elecrow uses SPI MODE3 for the ST7701 init
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
        // Panel-specific init from Elecrow's reference (the driver's built-in
        // default leaves this panel black).
        vendor_cfg.init_cmds = ELECROW_ST7701_INIT;
        vendor_cfg.init_cmds_size = sizeof(ELECROW_ST7701_INIT) / sizeof(ELECROW_ST7701_INIT[0]);

        esp_lcd_panel_dev_config_t panel_cfg = {};
        panel_cfg.reset_gpio_num = GPIO_NUM_NC;  // reset done via PCF8574 above
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
