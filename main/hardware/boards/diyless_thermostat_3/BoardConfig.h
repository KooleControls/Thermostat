#pragma once

// ──────────────────────────────────────────────────────────────
// Board configuration — DIYLESS OpenTherm Thermostat 3 (hw rev v3.3)
//   ESP32-S3 (QFN56, 8 MB flash DIO/quad, 8 MB octal PSRAM @ 80 MHz)
//   4.0" 480x480 IPS, ST7701S (3-wire SPI init + 16-bit RGB565 data)
//   GT911 capacitive touch over I2C (auto-probed 0x5D/0x14; this unit is 0x14)
//   AHT20 temp/humidity sensor shares the same I2C bus
//
// Pin map transcribed verbatim from the official DIYLESS ESPHome reference
// config (github.com/diyless/esphome-opentherm-thermostat → diyless-thermostat-3.yaml),
// supplied directly by DIYLESS (Ihor Melnyk).
//
// NOTE on shared pins: GPIO11/GPIO12 are the ST7701 3-wire-SPI init lines
// (MOSI/SCK) AND, after panel init, the serial link to the STM32L051 that owns
// the OpenTherm PHY (the ESPHome config marks them allow_other_uses; the
// ST7701 only needs SPI during init). GPIO44/GPIO13 are the STM32 BOOT0/NRST.
// This (dumb-display) firmware drives only the panel/touch/sensor; the STM32
// link is out of scope here.
// ──────────────────────────────────────────────────────────────

namespace BoardConfig
{
    // No separate user LED on this board.
    static constexpr int LED_PIN = -1;
    static constexpr bool LED_ACTIVE_HIGH = true;

    // ── Shared I2C bus (GT911 touch + AHT20 sensor) ────────────────
    static constexpr int I2C_PIN_SDA = 17;
    static constexpr int I2C_PIN_SCL = 18;

    // ── GT911 capacitive touch (I2C, shared bus) ───────────────────
    // Address is auto-probed (0x5D default, 0x14 backup) in Touch.h; this unit
    // reports 0x14. INT is wired to GPIO10 but we poll (handled by esp_lvgl_port),
    // and there is no ESP-controlled touch reset line.
    static constexpr int TOUCH_PIN_INT = 10;

    // ── RGB LCD panel (ST7701S) ────────────────────────────────────
    static constexpr int LCD_H_RES = 480;
    static constexpr int LCD_V_RES = 480;

    // Timing from diyless-thermostat-3.yaml.
    static constexpr int LCD_PIXEL_CLOCK_HZ = 10000000;  // 10 MHz
    static constexpr bool LCD_PCLK_ACTIVE_NEG = false;   // pclk_inverted: false
    static constexpr bool LCD_PCLK_IDLE_HIGH  = false;

    static constexpr int LCD_HSYNC_PULSE_WIDTH = 14;
    static constexpr int LCD_HSYNC_BACK_PORCH  = 14;
    static constexpr int LCD_HSYNC_FRONT_PORCH = 14;
    static constexpr int LCD_VSYNC_PULSE_WIDTH = 10;
    static constexpr int LCD_VSYNC_BACK_PORCH  = 14;
    static constexpr int LCD_VSYNC_FRONT_PORCH = 14;

    // RGB control signals (direct GPIO).
    static constexpr int LCD_PIN_DE    = 45;
    static constexpr int LCD_PIN_VSYNC = 4;
    static constexpr int LCD_PIN_HSYNC = 5;
    static constexpr int LCD_PIN_PCLK  = 21;

    // ST7701 hardware reset and backlight are direct GPIOs (no IO expander).
    static constexpr int LCD_PIN_RESET     = 43;
    static constexpr int LCD_PIN_BACKLIGHT = 46;   // active-high (LEDC on stock FW; on/off here)

    // 3-wire SPI used only for the ST7701 init sequence (all direct GPIO).
    static constexpr int LCD_SPI_CS  = 1;
    static constexpr int LCD_SPI_SCK = 12;
    static constexpr int LCD_SPI_SDA = 11;

    // Whether to enable display colour inversion (yaml: invert_colors: true).
    static constexpr bool LCD_INVERT_COLOR = true;

    // 16 RGB565 data lines in the order the esp_lcd RGB driver expects:
    //   data[0..4]   = B0..B4   (yaml "blue")
    //   data[5..10]  = G0..G5   (yaml "green")
    //   data[11..15] = R0..R4   (yaml "red")
    static constexpr int LCD_DATA_PINS[16] = {
        6, 7, 15, 16, 8,        // B0..B4
        0, 9, 14, 47, 48, 3,    // G0..G5
        39, 40, 41, 42, 2,      // R0..R4
    };
}
