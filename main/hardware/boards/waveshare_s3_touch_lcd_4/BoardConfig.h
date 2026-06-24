#pragma once

#include "esp_io_expander.h"   // IO_EXPANDER_PIN_NUM_* for the CH32V003 lines

// ──────────────────────────────────────────────────────────────
// Board configuration — Waveshare ESP32-S3-Touch-LCD-4
//   ESP32-S3R8 (16 MB flash, 8 MB octal PSRAM)
//   4.0" 480x480 IPS, ST7701 (3-wire SPI init + 16-bit RGB565 data)
//   GT911 capacitive touch over I2C @ 0x5D (or 0x14)
//
// IMPORTANT — IO expander: a Waveshare CH32V003 (a tiny MCU acting as an I2C
// IO expander) @ 0x24 owns the LCD reset, touch reset, system power-enable,
// buzzer, RTC-INT, and the backlight (driven by a PWM channel — there is NO
// backlight GPIO). The 3-wire SPI CS/SCK/SDA are direct GPIOs, so unlike the
// Elecrow board the SPI init does not go through the expander. See Expander.h.
//
// Pin map transcribed verbatim from Waveshare's official ESP-IDF BSP:
//   github.com/waveshareteam/Waveshare-ESP32-components
//     bsp/esp32_s3_touch_lcd_4  (board v3.0.0)
// The ST7701 init sequence and RGB timing are the panel-specific values from
// that BSP, so this should light up without the per-pixel bring-up the RGB
// clone boards needed — but RGB/BGR order and orientation are still worth a
// look on first power-up (see Display.h).
// ──────────────────────────────────────────────────────────────

namespace BoardConfig
{
    // No separate user LED on this board.
    static constexpr int LED_PIN = -1;
    static constexpr bool LED_ACTIVE_HIGH = true;

    // ── Shared I2C bus (GT911 touch + CH32V003 expander) ───────────
    static constexpr int I2C_PIN_SDA = 15;
    static constexpr int I2C_PIN_SCL = 7;

    // ── CH32V003 IO expander @ 0x24 (pin masks are IO_EXPANDER_PIN_NUM_*) ─
    static constexpr uint32_t EXP_I2C_ADDR  = 0x24;
    static constexpr uint32_t EXP_TOUCH_RST = IO_EXPANDER_PIN_NUM_1;  // EXIO1, active-low pulse
    static constexpr uint32_t EXP_LCD_RST   = IO_EXPANDER_PIN_NUM_3;  // EXIO3, active-low pulse
    static constexpr uint32_t EXP_SYS_EN    = IO_EXPANDER_PIN_NUM_5;  // EXIO5, drive high to power the panel
    static constexpr uint32_t EXP_BEE_EN    = IO_EXPANDER_PIN_NUM_6;  // EXIO6, buzzer enable (held off/low)
    static constexpr uint32_t EXP_RTC_INT   = IO_EXPANDER_PIN_NUM_7;  // EXIO7, RTC interrupt (input)

    // ── GT911 capacitive touch (I2C, shared bus) ───────────────────
    // Address is auto-probed (0x5D default, 0x14 backup) in Touch.h.
    // RST is on the expander (EXP_TOUCH_RST); INT is not wired to a GPIO.

    // ── RGB LCD panel (ST7701) ─────────────────────────────────────
    static constexpr int LCD_H_RES = 480;
    static constexpr int LCD_V_RES = 480;

    // Values from the BSP's ST7701_480_480_PANEL_60HZ_RGB_TIMING().
    static constexpr int LCD_PIXEL_CLOCK_HZ = 16000000;
    static constexpr bool LCD_PCLK_ACTIVE_NEG = false;  // rising edge
    static constexpr bool LCD_PCLK_IDLE_HIGH  = false;

    static constexpr int LCD_HSYNC_PULSE_WIDTH = 10;
    static constexpr int LCD_HSYNC_BACK_PORCH  = 10;
    static constexpr int LCD_HSYNC_FRONT_PORCH = 20;
    static constexpr int LCD_VSYNC_PULSE_WIDTH = 10;
    static constexpr int LCD_VSYNC_BACK_PORCH  = 10;
    static constexpr int LCD_VSYNC_FRONT_PORCH = 10;

    // RGB control signals
    static constexpr int LCD_PIN_DE    = 40;
    static constexpr int LCD_PIN_VSYNC = 39;
    static constexpr int LCD_PIN_HSYNC = 38;
    static constexpr int LCD_PIN_PCLK  = 41;

    // 3-wire SPI used only for the ST7701 init sequence (all direct GPIO).
    static constexpr int LCD_SPI_CS  = 42;
    static constexpr int LCD_SPI_SCK = 2;
    static constexpr int LCD_SPI_SDA = 1;
    // LCD hardware reset is on the expander (EXP_LCD_RST); backlight is the
    // expander's PWM channel (no GPIO) — see Expander.h.

    // 16 RGB565 data lines, ordered as the esp_lcd RGB driver expects:
    //   data[0..4]  = B0..B4
    //   data[5..10] = G0..G5
    //   data[11..15]= R0..R4
    static constexpr int LCD_DATA_PINS[16] = {
        5, 45, 48, 47, 21,      // B0..B4
        14, 13, 12, 11, 10, 9,  // G0..G5
        46, 3, 8, 18, 17,       // R0..R4
    };
}
