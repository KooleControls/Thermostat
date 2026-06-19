#pragma once

// ──────────────────────────────────────────────────────────────
// Board configuration — Elecrow CrowPanel 2.1" HMI Rotary Display
//   ESP32-S3R8 (16 MB flash, 8 MB octal PSRAM)
//   2.1" 480x480 round IPS, ST7701S (3-wire SPI init + 16-bit RGB565 data)
//   CST826 capacitive touch (CST816 family) over I2C @ 0x15
//   Rotary knob: quadrature encoder + push-button
//
// IMPORTANT — IO expander: a PCF8574 @ 0x21 (on the shared I2C bus) holds the
// LCD power-enable, LCD reset, touch reset/INT, and the encoder push-button.
// Those lines are NOT on direct ESP32 GPIOs. See Pcf8574.h / Display.h.
//
// Pin map cross-checked against Elecrow's official Arduino demo
// (RotaryScreen_2_1.ino) and ESPHome configs:
//   github.com/Elecrow-RD/CrowPanel-2.1inch-HMI-ESP32-Rotary-Display-...
//
// NOTE: the panel/touch/knob drivers in this folder are a faithful first draft
// from that reference and still need on-hardware bring-up (esp. the ST7701S
// init sequence, RGB/BGR color order, and porch timings — see Display.h).
// ──────────────────────────────────────────────────────────────

namespace BoardConfig
{
    // LED — no usable separate user LED (GPIO43 is the UART TXD pin). Disable.
    static constexpr int LED_PIN = -1;
    static constexpr bool LED_ACTIVE_HIGH = true;

    // ── Shared I2C bus (touch + PCF8574 expander) ──────────────────
    static constexpr int I2C_PIN_SDA = 38;
    static constexpr int I2C_PIN_SCL = 39;

    // ── PCF8574 IO expander @ 0x21 (pin numbers are P0..P7) ─────────
    static constexpr int PCF8574_I2C_ADDR   = 0x21;
    static constexpr int PCF_TOUCH_RST       = 0;  // P0 (output)
    static constexpr int PCF_TOUCH_INT       = 2;  // P2 (held high)
    static constexpr int PCF_LCD_POWER       = 3;  // P3 (output, active-high)
    static constexpr int PCF_LCD_RST         = 4;  // P4 (output, active-low pulse)
    static constexpr int PCF_ENCODER_SW      = 5;  // P5 (input pull-up, active-low)

    // ── CST826 capacitive touch (I2C) ──────────────────────────────
    static constexpr int TOUCH_I2C_ADDR = 0x15;
    // RST/INT are on the PCF8574 (P0/P2), not GPIOs.

    // ── RGB LCD panel (ST7701S) ────────────────────────────────────
    static constexpr int LCD_H_RES = 480;
    static constexpr int LCD_V_RES = 480;

    // ESPHome's documented values (Arduino demo uses slightly different porches
    // — start here and confirm on hardware).
    static constexpr int LCD_PIXEL_CLOCK_HZ = 18000000;
    static constexpr bool LCD_PCLK_ACTIVE_NEG = true;   // pclk_inverted
    static constexpr bool LCD_PCLK_IDLE_HIGH  = false;

    static constexpr int LCD_HSYNC_PULSE_WIDTH = 10;
    static constexpr int LCD_HSYNC_BACK_PORCH  = 10;
    static constexpr int LCD_HSYNC_FRONT_PORCH = 20;
    static constexpr int LCD_VSYNC_PULSE_WIDTH = 10;
    static constexpr int LCD_VSYNC_BACK_PORCH  = 10;
    static constexpr int LCD_VSYNC_FRONT_PORCH = 8;

    // RGB control signals
    static constexpr int LCD_PIN_DE    = 40;
    static constexpr int LCD_PIN_VSYNC = 7;
    static constexpr int LCD_PIN_HSYNC = 15;
    static constexpr int LCD_PIN_PCLK  = 41;

    // 3-wire SPI used only for the ST7701S init sequence (CS/SCL/SDA direct GPIO)
    static constexpr int LCD_SPI_CS  = 16;
    static constexpr int LCD_SPI_SCK = 2;
    static constexpr int LCD_SPI_SDA = 1;
    // LCD hardware reset is on PCF8574 P4 (see PCF_LCD_RST).

    // Backlight (active high, PWM-capable). Drive high to turn the panel on.
    static constexpr int LCD_PIN_BACKLIGHT = 6;

    // 16 RGB565 data lines, ordered as the esp_lcd RGB driver expects:
    //   data[0..4]  = B0..B4
    //   data[5..10] = G0..G5
    //   data[11..15]= R0..R4
    static constexpr int LCD_DATA_PINS[16] = {
        5, 45, 48, 47, 21,      // B0..B4
        14, 13, 12, 11, 10, 9,  // G0..G5
        46, 3, 8, 18, 17,       // R0..R4
    };

    // ── Rotary knob ────────────────────────────────────────────────
    static constexpr int ENCODER_PIN_A = 42;
    static constexpr int ENCODER_PIN_B = 4;
    // Push-button is on PCF8574 P5 (active-low), read via the expander.
}
