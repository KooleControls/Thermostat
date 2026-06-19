#pragma once

// ──────────────────────────────────────────────────────────────
// Board configuration — VIEWE UEDX48480021-MD80ET
//   (sold generically as "ESP32-S3 2.1\" Round Rotary Knob 480x480", Spotpear)
//   ESP32-S3R8 (16 MB flash, 8 MB octal PSRAM)
//   2.1" 480x480 round IPS, ST7701S (3-wire SPI init + 16-bit RGB565 data)
//   CST826 capacitive touch (CST816 family) over I2C @ 0x15
//   Rotary knob: quadrature encoder + push-button (all direct GPIO, NO expander)
//
// Pin map from VIEWE's own ESP-IDF BSP (examples/ESP-IDF .../bsp):
//   github.com/VIEWESMART/UEDX48480021-MD80ESP32-2.1inch-Touch-Knob-Display
//
// Quirk: the 3-wire SPI init pins SCK(13)/SDA(12) are SHARED with two RGB blue
// data lines. The ST7701 driver runs the SPI init first, then releases those
// pins to the RGB bus — enabled via the vendor-config multiplex flag (Display.h).
// ──────────────────────────────────────────────────────────────

namespace BoardConfig
{
    // No separate user LED (GPIO0 is the encoder button).
    static constexpr int LED_PIN = -1;
    static constexpr bool LED_ACTIVE_HIGH = true;

    // ── CST826 capacitive touch (own I2C bus; no IO expander) ──────
    static constexpr int TOUCH_PIN_SDA = 16;
    static constexpr int TOUCH_PIN_SCL = 15;
    static constexpr int TOUCH_I2C_ADDR = 0x15;
    // RST/INT are not wired to MCU GPIOs (touch shares the LCD reset).

    // ── RGB LCD panel (ST7701S) ────────────────────────────────────
    static constexpr int LCD_H_RES = 480;
    static constexpr int LCD_V_RES = 480;

    // Timings from the vendor BSP "with-touch" profile.
    static constexpr int LCD_PIXEL_CLOCK_HZ = 26000000;  // vendor value (fixes artifacts)
    static constexpr bool LCD_PCLK_ACTIVE_NEG = false;   // rising edge, not inverted
    static constexpr bool LCD_PCLK_IDLE_HIGH  = false;

    static constexpr int LCD_HSYNC_PULSE_WIDTH = 8;
    static constexpr int LCD_HSYNC_BACK_PORCH  = 20;
    static constexpr int LCD_HSYNC_FRONT_PORCH = 40;
    static constexpr int LCD_VSYNC_PULSE_WIDTH = 8;
    static constexpr int LCD_VSYNC_BACK_PORCH  = 20;
    static constexpr int LCD_VSYNC_FRONT_PORCH = 50;

    // RGB control signals
    static constexpr int LCD_PIN_DE    = 17;
    static constexpr int LCD_PIN_VSYNC = 3;
    static constexpr int LCD_PIN_HSYNC = 46;
    static constexpr int LCD_PIN_PCLK  = 9;

    // 3-wire SPI used for the ST7701S init (SCK/SDA shared with RGB blue bits;
    // released to the RGB bus after init via the multiplex flag).
    static constexpr int LCD_SPI_CS  = 18;
    static constexpr int LCD_SPI_SCK = 13;
    static constexpr int LCD_SPI_SDA = 12;
    static constexpr int LCD_PIN_RST = 8;   // direct GPIO, active low

    // Backlight on GPIO7, ACTIVE-LOW (drive low to turn the panel on).
    static constexpr int LCD_PIN_BACKLIGHT = 7;
    static constexpr bool LCD_BACKLIGHT_ACTIVE_HIGH = false;

    // 16 RGB565 data lines, ordered as the esp_lcd RGB driver expects:
    //   data[0..4]  = B0..B4
    //   data[5..10] = G0..G5
    //   data[11..15]= R0..R4
    // Per-line probe on this (clone) board showed the bus is wired:
    //   IO21,47,48,45,38,39 -> BLUE (6 lines)
    //   IO40,41,42,2,1       -> GREEN (5 lines)
    //   IO10,11,12,13,14     -> "dark" group (where RED should be)
    // esp_lcd RGB565 slots are data[0..4]=B(5), data[5..10]=G(6), data[11..15]=R(5).
    // Blue has 6 physical lines but the B slot is 5, and green has 5 lines but the
    // G slot is 6 — so the spare blue line (IO39) rides in the green-LSB slot
    // (minor tint). Intra-channel order is arbitrary (UI uses solid colors).
    static constexpr int LCD_DATA_PINS[16] = {
        21, 47, 48, 45, 38,     // B0..B4  (blue)
        39, 40, 41, 42, 2, 1,   // G0..G5  (IO39 spare-blue as green LSB + 5 green)
        10, 11, 12, 13, 14,     // R0..R4  (the "dark" group — test if it's red)
    };

    // ── Rotary knob (all direct GPIO) ──────────────────────────────
    static constexpr int ENCODER_PIN_A = 6;
    static constexpr int ENCODER_PIN_B = 5;
    static constexpr int ENCODER_PIN_SW = 0;  // GPIO0 (BOOT), active-low, board pull-up
}
