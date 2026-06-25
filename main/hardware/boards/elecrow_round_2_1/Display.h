#pragma once

#include "BoardConfig.h"
#include "Pcf8574.h"
#include "drivers/St7701Panel.h"
#include "driver/gpio.h"
#include "esp_lcd_st7701.h"   // for the st7701_lcd_init_cmd_t type
#include "esp_log.h"

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

    esp_lcd_panel_handle_t panel() const { return drv_.panel(); }

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
        St7701Config cfg{};
        cfg.spi_cs = (gpio_num_t)BoardConfig::LCD_SPI_CS;
        cfg.spi_sck = (gpio_num_t)BoardConfig::LCD_SPI_SCK;
        cfg.spi_sda = (gpio_num_t)BoardConfig::LCD_SPI_SDA;
        cfg.spi_mode = 3;   // Elecrow uses SPI MODE3 for the ST7701 init

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
        cfg.clk_src = LCD_CLK_SRC_DEFAULT;
        cfg.dma_burst_size = 64;

        cfg.reset_gpio = GPIO_NUM_NC;  // reset done via the PCF8574 above
        cfg.rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB;
        // Panel-specific init from Elecrow's reference (the driver's built-in
        // default leaves this panel black).
        cfg.init_cmds = ELECROW_ST7701_INIT;
        cfg.init_cmds_size = sizeof(ELECROW_ST7701_INIT) / sizeof(ELECROW_ST7701_INIT[0]);
        cfg.mirror_by_cmd = false;

        return drv_.Init(cfg);
    }

    St7701Panel drv_;
};
