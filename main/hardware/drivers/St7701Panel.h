#pragma once

#include "driver/gpio.h"
#include "esp_lcd_panel_rgb.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_st7701.h"
#include "esp_lcd_panel_io_additions.h"
#include "esp_log.h"
#include "esp_err.h"
#include <cstddef>

// ──────────────────────────────────────────────────────────────
// Reusable driver — Sitronix ST7701(S) 16-bit RGB565 panel via the
// esp_lcd_st7701 component. The ST7701 takes a panel-specific init sequence
// over a 3-wire SPI line (CS/SCK/SDA on direct GPIOs) and then streams pixels
// over the 16-bit RGB parallel bus.
//
// Everything that varies between boards is in St7701Config: the SPI pins +
// mode, the RGB pin map + timings, the clock source, and the panel-specific
// init command table. Power/reset/backlight wiring (direct GPIO vs IO
// expander) stays in each board's Display.h — the board prepares the panel,
// then hands a filled config to this driver.
//
// The 3-wire SPI io config is built explicitly rather than via
// ST7701_PANEL_IO_3WIRE_SPI_CONFIG() because that macro's C designated-
// initializer order doesn't hold in C++.
// ──────────────────────────────────────────────────────────────

struct St7701Config
{
    // 3-wire SPI init line (all direct GPIO; CS is never on an IO expander here)
    gpio_num_t spi_cs = GPIO_NUM_NC;
    gpio_num_t spi_sck = GPIO_NUM_NC;
    gpio_num_t spi_sda = GPIO_NUM_NC;
    int spi_mode = 0;   // 0 (MODE0) for most; Elecrow uses 3 (MODE3)

    // 16-bit RGB parallel bus
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
    lcd_clock_source_t clk_src = LCD_CLK_SRC_DEFAULT;   // DIYLESS pins PLL160M
    size_t dma_burst_size = 0;   // 0 → driver default

    // Framebuffer layout. Two shapes are useful here:
    //
    //   num_fbs 1 + bounce_buffer_lines N
    //     One framebuffer in PSRAM, DMA fed from a pair of small internal
    //     bounce buffers refilled by a GDMA EOF interrupt every N lines. Cheap
    //     on PSRAM, but the refill is a CPU memcpy out of PSRAM inside an ISR
    //     with a hard deadline of N lines of scanout; miss it and the panel is
    //     fed from the wrong offset and the image slips vertically.
    //
    //   num_fbs 2 + bounce_buffer_lines 0
    //     Two framebuffers in PSRAM, DMA streaming straight from them, no
    //     interrupt and no deadline to miss. Lets LVGL render directly into the
    //     buffers and swap on VSYNC — one render pass instead of one per
    //     bounce-buffer's worth of lines, no staging copy, and no tearing.
    //     Costs a second full framebuffer of PSRAM.
    size_t num_fbs = 1;
    int bounce_buffer_lines = 10;   // 0 disables bounce-buffer mode entirely

    // ST7701 panel device
    gpio_num_t reset_gpio = GPIO_NUM_NC;   // NC when reset is via an IO expander
    lcd_rgb_element_order_t rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB;
    int bits_per_pixel = 16;
    const st7701_lcd_init_cmd_t *init_cmds = nullptr;
    size_t init_cmds_size = 0;
    bool mirror_by_cmd = false;
    // Delete the 3-wire-SPI panel IO after the init sequence runs, freeing its
    // CS/SCK/SDA GPIOs for other use. Needed on boards that reuse those pins
    // (e.g. DIYLESS Thermostat 3 shares SCK/SDA with the STM32 OpenTherm UART).
    // Leave false unless the panel needs no runtime command writes (mirror/etc).
    bool auto_del_panel_io = false;
};

class St7701Panel
{
    static constexpr const char *TAG = "Display";

public:
    bool Init(const St7701Config &cfg)
    {
        // 3-wire SPI panel IO for the ST7701 init sequence.
        spi_line_config_t line_config = {};
        line_config.cs_io_type = IO_TYPE_GPIO;
        line_config.cs_gpio_num = cfg.spi_cs;
        line_config.scl_io_type = IO_TYPE_GPIO;
        line_config.scl_gpio_num = cfg.spi_sck;
        line_config.sda_io_type = IO_TYPE_GPIO;
        line_config.sda_gpio_num = cfg.spi_sda;
        line_config.io_expander = nullptr;  // CS is a direct GPIO on these boards

        esp_lcd_panel_io_3wire_spi_config_t io_config = {};
        io_config.line_config = line_config;
        io_config.expect_clk_speed = PANEL_IO_3WIRE_SPI_CLK_MAX;
        io_config.spi_mode = cfg.spi_mode;
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
        rgb_cfg.clk_src = cfg.clk_src;
        rgb_cfg.data_width = 16;
        rgb_cfg.num_fbs = cfg.num_fbs;
        rgb_cfg.bounce_buffer_size_px = cfg.bounce_buffer_lines > 0
                                            ? cfg.h_res * cfg.bounce_buffer_lines
                                            : 0;
        rgb_cfg.dma_burst_size = cfg.dma_burst_size;  // 0 keeps the driver default
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

        st7701_vendor_config_t vendor_cfg = {};
        vendor_cfg.rgb_config = &rgb_cfg;
        vendor_cfg.init_cmds = cfg.init_cmds;
        vendor_cfg.init_cmds_size = cfg.init_cmds_size;
        vendor_cfg.flags.auto_del_panel_io = cfg.auto_del_panel_io ? 1 : 0;
        vendor_cfg.flags.mirror_by_cmd = cfg.mirror_by_cmd ? 1 : 0;

        esp_lcd_panel_dev_config_t panel_cfg = {};
        panel_cfg.reset_gpio_num = cfg.reset_gpio;
        panel_cfg.rgb_ele_order = cfg.rgb_ele_order;
        panel_cfg.bits_per_pixel = cfg.bits_per_pixel;
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

    esp_lcd_panel_handle_t panel() const { return panel_; }
    bool ok() const { return panel_ != nullptr; }

private:
    esp_lcd_panel_handle_t panel_ = nullptr;
};
