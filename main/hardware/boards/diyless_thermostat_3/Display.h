#pragma once

#include "BoardConfig.h"
#include "drivers/St7701Panel.h"
#include "driver/gpio.h"
#include "esp_lcd_st7701.h"   // for the st7701_lcd_init_cmd_t type
#include "esp_log.h"

// ST7701S init sequence for the DIYLESS panel, transcribed verbatim from the
// ESPHome st7701s component's default sequence #1 (ST7701S_1_INIT in
// esphome/components/st7701s/init_sequences.py) — the sequence DIYLESS's own
// ESPHome config relies on. The esp_lcd driver's generic *default* init does
// NOT match this panel and produces vertical banding; these panel-specific
// values (0xC0 line count, 0xC2 inversion/frame-rate, BK1 scan regs, the BK3
// 0xE5=0xE4, COLMOD 0x3A=0x60) are what make it lock correctly.
//
// Structured like the proven Waveshare ST7701 driver in this repo: the four
// 0xFF writes are CMD2 bank selects (BK0/BK1/BK3/CMD1), and we append explicit
// MADCTL (0x36), COLMOD (0x3A), inversion-on (0x21, since invert_colors: true)
// and display-on (0x29). The leading 0x11 is sleep-out.
static const st7701_lcd_init_cmd_t DIYLESS_ST7701_INIT[] = {
    {0x11, (uint8_t[]){0x00}, 0, 120},   // sleep out

    // CMD2 BK0
    {0xFF, (uint8_t[]){0x77, 0x01, 0x00, 0x00, 0x10}, 5, 0},
    {0xC0, (uint8_t[]){0x3B, 0x00}, 2, 0},   // display line count -> 480
    {0xC1, (uint8_t[]){0x0D, 0x02}, 2, 0},
    {0xC2, (uint8_t[]){0x31, 0x05}, 2, 0},   // inversion / frame rate (panel-specific)
    {0xCD, (uint8_t[]){0x08}, 1, 0},
    {0xB0, (uint8_t[]){0x00, 0x11, 0x18, 0x0E, 0x11, 0x06, 0x07, 0x08, 0x07, 0x22, 0x04, 0x12, 0x0F, 0xAA, 0x31, 0x18}, 16, 0},
    {0xB1, (uint8_t[]){0x00, 0x11, 0x19, 0x0E, 0x12, 0x07, 0x08, 0x08, 0x08, 0x22, 0x04, 0x11, 0x11, 0xA9, 0x32, 0x18}, 16, 0},

    // CMD2 BK1
    {0xFF, (uint8_t[]){0x77, 0x01, 0x00, 0x00, 0x11}, 5, 0},
    {0xB0, (uint8_t[]){0x60}, 1, 0},
    {0xB1, (uint8_t[]){0x32}, 1, 0},
    {0xB2, (uint8_t[]){0x07}, 1, 0},
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

    // CMD2 BK3 (DIYLESS/ESPHome-specific)
    {0xFF, (uint8_t[]){0x77, 0x01, 0x00, 0x00, 0x13}, 5, 0},
    {0xE5, (uint8_t[]){0xE4}, 1, 0},

    // Back to CMD1 (user command set)
    {0xFF, (uint8_t[]){0x77, 0x01, 0x00, 0x00, 0x00}, 5, 0},
    {0x36, (uint8_t[]){0x00}, 1, 0},     // MADCTL — RGB, no mirror (flip here if orientation wrong)
    {0x3A, (uint8_t[]){0x60}, 1, 0},     // COLMOD (per ESPHome seq #1)
    {0x21, (uint8_t[]){0x00}, 0, 0},     // display inversion ON (invert_colors: true)
    {0x29, (uint8_t[]){0x00}, 0, 120},   // display ON
};

// ──────────────────────────────────────────────────────────────
// ST7701S 480x480 RGB panel for the DIYLESS Thermostat 3.
//
// The ST7701S is configured over a 3-wire SPI init sequence (CS/SCK/SDA on
// direct GPIOs) and then streams pixels over a 16-bit RGB565 parallel bus
// (same RGB DMA path as the other RGB boards). Unlike the Waveshare board,
// LCD reset and backlight are plain GPIOs (no IO expander).
//
// Init sequence: we use the esp_lcd_st7701 driver's built-in default
// (init_cmds = nullptr). The DIYLESS ESPHome config likewise relies on the
// st7701s platform default (no custom init_sequence), so this should light up.
// If colours/gamma look off on bring-up, transcribe a panel-specific sequence
// here (see the Waveshare board's WAVESHARE_ST7701_INIT for the shape).
//
// ⚠ Bring-up checklist (first power-on):
//   • Colour element order — start RGB (per yaml color_order: RGB); flip to
//     BGR via panel_cfg.rgb_ele_order if red/blue are swapped.
//   • Inversion — yaml sets invert_colors: true (applied below).
//   • Orientation — adjust via lvgl_port rotation in DisplayManager if needed.
// ──────────────────────────────────────────────────────────────

class Display
{
    static constexpr const char *TAG = "Display";

public:
    bool Init()
    {
        InitBacklight();          // configure + hold off until first frame
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
    void InitBacklight()
    {
        gpio_config_t bk = {};
        bk.pin_bit_mask = 1ULL << BoardConfig::LCD_PIN_BACKLIGHT;
        bk.mode = GPIO_MODE_OUTPUT;
        gpio_config(&bk);
        gpio_set_level((gpio_num_t)BoardConfig::LCD_PIN_BACKLIGHT, 0);  // dark until first frame
    }

    bool InitPanel()
    {
        St7701Config cfg{};
        cfg.spi_cs = (gpio_num_t)BoardConfig::LCD_SPI_CS;
        cfg.spi_sck = (gpio_num_t)BoardConfig::LCD_SPI_SCK;
        cfg.spi_sda = (gpio_num_t)BoardConfig::LCD_SPI_SDA;
        cfg.spi_mode = 0;

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
        // Match the DIYLESS ESPHome st7701s driver exactly: it pins the RGB
        // pixel clock to the 160 MHz PLL (not LCD_CLK_SRC_DEFAULT). A jittery
        // clock source shows up as timing-edge artifacts — worst in the screen
        // corners — and faint vertical banding. ESPHome leaves dma_burst_size
        // at the driver default, so we do too (cfg.dma_burst_size stays 0).
        cfg.clk_src = LCD_CLK_SRC_PLL160M;

        cfg.reset_gpio = (gpio_num_t)BoardConfig::LCD_PIN_RESET;  // direct GPIO reset
        cfg.rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB;
        cfg.init_cmds = DIYLESS_ST7701_INIT;   // ESPHome seq #1 — matches this panel
        cfg.init_cmds_size = sizeof(DIYLESS_ST7701_INIT) / sizeof(DIYLESS_ST7701_INIT[0]);
        cfg.mirror_by_cmd = true;
        // Inversion is applied by the init sequence (0x21); no separate call.

        return drv_.Init(cfg);
    }

    St7701Panel drv_;
};
