#pragma once

#include "BoardConfig.h"
#include "driver/gpio.h"
#include "esp_lcd_panel_rgb.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_st7701.h"   // only for the st7701_lcd_init_cmd_t struct type
#include "esp_log.h"
#include "esp_err.h"
#include "esp_rom_sys.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// ST7701S init sequence for this exact panel, transcribed from VIEWE's ESP-IDF
// BSP (bsp_lcd.c). The 0xFF writes are CMD2 bank-selects and must stay ordered.
// Ends with sleep-out (0x11) then display-on (0x29).
static const st7701_lcd_init_cmd_t VIEWE_ST7701_INIT[] = {
    {0xFF, (uint8_t[]){0x77, 0x01, 0x00, 0x00, 0x13}, 5, 0},
    {0xEF, (uint8_t[]){0x08}, 1, 0},
    {0xFF, (uint8_t[]){0x77, 0x01, 0x00, 0x00, 0x10}, 5, 0},
    {0xC0, (uint8_t[]){0x3B, 0x00}, 2, 0},
    {0xC1, (uint8_t[]){0x0B, 0x02}, 2, 0},
    {0xC2, (uint8_t[]){0x07, 0x02}, 2, 0},
    {0xC7, (uint8_t[]){0x00}, 1, 0},    // SDIR: horizontal scan direction (0x00<->0x04 un-mirrors L/R)
    {0xCC, (uint8_t[]){0x10}, 1, 0},
    {0xCD, (uint8_t[]){0x08}, 1, 0},
    {0xB0, (uint8_t[]){0x00, 0x11, 0x16, 0x0E, 0x11, 0x06, 0x05, 0x09, 0x08, 0x21, 0x06, 0x13, 0x10, 0x29, 0x31, 0x18}, 16, 0},
    {0xB1, (uint8_t[]){0x00, 0x11, 0x16, 0x0E, 0x11, 0x07, 0x05, 0x09, 0x09, 0x21, 0x05, 0x13, 0x11, 0x2A, 0x31, 0x18}, 16, 0},
    {0xFF, (uint8_t[]){0x77, 0x01, 0x00, 0x00, 0x11}, 5, 0},
    {0xB0, (uint8_t[]){0x6D}, 1, 0},
    {0xB1, (uint8_t[]){0x37}, 1, 0},
    {0xB2, (uint8_t[]){0x8B}, 1, 0},
    {0xB3, (uint8_t[]){0x80}, 1, 0},
    {0xB5, (uint8_t[]){0x43}, 1, 0},
    {0xB7, (uint8_t[]){0x85}, 1, 0},
    {0xB8, (uint8_t[]){0x20}, 1, 0},
    {0xC0, (uint8_t[]){0x09}, 1, 0},
    {0xC1, (uint8_t[]){0x78}, 1, 0},
    {0xC2, (uint8_t[]){0x78}, 1, 0},
    {0xD0, (uint8_t[]){0x88}, 1, 0},
    {0xE0, (uint8_t[]){0x00, 0x00, 0x02}, 3, 0},
    {0xE1, (uint8_t[]){0x03, 0xA0, 0x00, 0x00, 0x04, 0xA0, 0x00, 0x00, 0x00, 0x20, 0x20}, 11, 0},
    {0xE2, (uint8_t[]){0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, 13, 0},
    {0xE3, (uint8_t[]){0x00, 0x00, 0x11, 0x00}, 4, 0},
    {0xE4, (uint8_t[]){0x22, 0x00}, 2, 0},
    {0xE5, (uint8_t[]){0x05, 0xEC, 0xF6, 0xCA, 0x07, 0xEE, 0xF6, 0xCA, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, 16, 0},
    {0xE6, (uint8_t[]){0x00, 0x00, 0x11, 0x00}, 4, 0},
    {0xE7, (uint8_t[]){0x22, 0x00}, 2, 0},
    {0xE8, (uint8_t[]){0x06, 0xED, 0xF6, 0xCA, 0x08, 0xEF, 0xF6, 0xCA, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, 16, 0},
    {0xE9, (uint8_t[]){0x36, 0x00}, 2, 0},
    {0xEB, (uint8_t[]){0x00, 0x00, 0x40, 0x40, 0x00, 0x00, 0x00}, 7, 0},
    {0xED, (uint8_t[]){0xFF, 0xFF, 0xFF, 0xBA, 0x0A, 0xFF, 0x45, 0xFF, 0xFF, 0x54, 0xFF, 0xA0, 0xAB, 0xFF, 0xFF, 0xFF}, 16, 0},
    {0xEF, (uint8_t[]){0x08, 0x08, 0x08, 0x45, 0x3F, 0x54}, 6, 0},
    {0xFF, (uint8_t[]){0x77, 0x01, 0x00, 0x00, 0x13}, 5, 0},
    {0xE8, (uint8_t[]){0x00, 0x0E}, 2, 0},
    {0xFF, (uint8_t[]){0x77, 0x01, 0x00, 0x00, 0x00}, 5, 0},
    {0x11, (uint8_t[]){0x00}, 0, 120},  // sleep out
    {0xFF, (uint8_t[]){0x77, 0x01, 0x00, 0x00, 0x13}, 5, 0},
    {0xE8, (uint8_t[]){0x00, 0x0C}, 2, 10},
    {0xE8, (uint8_t[]){0x00, 0x00}, 2, 0},
    {0xFF, (uint8_t[]){0x77, 0x01, 0x00, 0x00, 0x00}, 5, 0},
    {0x36, (uint8_t[]){0x00}, 1, 0},    // MADCTL (ignored for scan/color in RGB mode; see 0xC7)
    {0x3A, (uint8_t[]){0x77}, 1, 0},    // COLMOD (vendor value)
    {0x29, (uint8_t[]){0x00}, 0, 20},   // display on
};

// ──────────────────────────────────────────────────────────────
// ST7701S 480x480 round RGB panel for the VIEWE UEDX48480021-MD80ET.
//
// This mirrors the vendor's own ESP-IDF BSP exactly: the ST7701S init sequence
// is pushed over a manually bit-banged 9-bit 3-wire SPI (CS/SCK/SDA), then SCK
// and SDA are released back to the RGB bus (they double as two RGB data lines),
// and a plain esp_lcd RGB panel streams pixels. We deliberately do NOT use the
// esp_lcd_st7701 component here — it injects its own COLMOD/MADCTL before the
// init, which corrupted the colours on this panel. Panel reset is a direct
// GPIO (active low); backlight is active-low.
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
        int level = (on == BoardConfig::LCD_BACKLIGHT_ACTIVE_HIGH) ? 1 : 0;
        gpio_set_level((gpio_num_t)BoardConfig::LCD_PIN_BACKLIGHT, level);
    }

    static constexpr int Width()  { return BoardConfig::LCD_H_RES; }
    static constexpr int Height() { return BoardConfig::LCD_V_RES; }

private:
    // ── Bit-banged 9-bit 3-wire SPI (D/C in bit 8), SPI MODE3 (idle high) ──
    static void Sck(int v) { gpio_set_level((gpio_num_t)BoardConfig::LCD_SPI_SCK, v); }
    static void Sda(int v) { gpio_set_level((gpio_num_t)BoardConfig::LCD_SPI_SDA, v); }
    static void Cs(int v)  { gpio_set_level((gpio_num_t)BoardConfig::LCD_SPI_CS, v); }

    static void SpiWrite9(uint16_t v)  // 9 bits, MSB first; bit 8 = D/C
    {
        for (int n = 0; n < 9; ++n)
        {
            Sda((v & 0x0100) ? 1 : 0);
            v <<= 1;
            Sck(0);
            esp_rom_delay_us(10);
            Sck(1);
            esp_rom_delay_us(10);
        }
    }

    // One CS-framed 9-bit word — matches the vendor, which pulses CS per byte
    // (cmd and EACH data byte get their own CS low/high). The esp_lcd 3-wire
    // component (and holding CS across a whole command) latches differently and
    // corrupts this panel's init.
    static void Xfer(uint16_t v9)
    {
        Cs(0);
        esp_rom_delay_us(10);
        SpiWrite9(v9);
        esp_rom_delay_us(10);
        Cs(1);
        Sck(1);
        Sda(1);
        esp_rom_delay_us(10);
    }

    void SendInit()
    {
        // CS/SCK/SDA + RST as outputs.
        gpio_config_t io = {};
        io.mode = GPIO_MODE_OUTPUT;
        io.pin_bit_mask = (1ULL << BoardConfig::LCD_SPI_CS) | (1ULL << BoardConfig::LCD_SPI_SCK) |
                          (1ULL << BoardConfig::LCD_SPI_SDA) | (1ULL << BoardConfig::LCD_PIN_RST);
        gpio_config(&io);

        Cs(1); Sck(1); Sda(1);

        // Hardware reset (active low).
        gpio_set_level((gpio_num_t)BoardConfig::LCD_PIN_RST, 1);
        vTaskDelay(pdMS_TO_TICKS(10));
        gpio_set_level((gpio_num_t)BoardConfig::LCD_PIN_RST, 0);
        vTaskDelay(pdMS_TO_TICKS(10));
        gpio_set_level((gpio_num_t)BoardConfig::LCD_PIN_RST, 1);
        vTaskDelay(pdMS_TO_TICKS(120));

        // Push the init table: command (D/C=0), then its data bytes (D/C=1).
        // Each byte is its own CS-framed word (vendor behaviour).
        const size_t n = sizeof(VIEWE_ST7701_INIT) / sizeof(VIEWE_ST7701_INIT[0]);
        for (size_t i = 0; i < n; ++i)
        {
            const st7701_lcd_init_cmd_t &c = VIEWE_ST7701_INIT[i];
            Xfer((uint16_t)c.cmd);  // bit 8 = 0 -> command
            const uint8_t *d = (const uint8_t *)c.data;
            for (size_t j = 0; j < c.data_bytes; ++j)
                Xfer(0x0100 | d[j]);  // bit 8 = 1 -> data
            if (c.delay_ms) vTaskDelay(pdMS_TO_TICKS(c.delay_ms));
        }

        // Release SCK/SDA — they double as RGB data lines (GPIO13/12). CS stays.
        gpio_reset_pin((gpio_num_t)BoardConfig::LCD_SPI_SCK);
        gpio_reset_pin((gpio_num_t)BoardConfig::LCD_SPI_SDA);
    }

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
        // 1) Bit-bang the ST7701 init over 3-wire SPI, then free SCK/SDA.
        SendInit();

        // 2) Plain RGB panel over the 16-bit parallel bus (SCK/SDA now reused
        //    as two of the RGB data lines). No esp_lcd_st7701 component.
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

    esp_lcd_panel_handle_t panel_ = nullptr;
};
