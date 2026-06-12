#pragma once

// ──────────────────────────────────────────────────────────────
// Board configuration — hardware-specific pin assignments and constants.
// Edit this file to match your board or target MCU.
// ──────────────────────────────────────────────────────────────

namespace BoardConfig
{
    // ──────────────────────────────────────────────────────────────
    // Board: Sunton ESP32-8048S043(C)
    //   ESP32-S3 (N16R8: 16 MB flash, 8 MB octal PSRAM)
    //   4.3" 800x480 IPS, 16-bit RGB565 parallel LCD
    //   GT911 capacitive touch over I2C
    // ──────────────────────────────────────────────────────────────

    // LED — this board has no separate user LED; the LCD backlight is on GPIO2.
    // Left here for the (unused) LED driver; set to -1 to disable.
    static constexpr int LED_PIN = -1;
    static constexpr bool LED_ACTIVE_HIGH = true;

    // ── RGB LCD panel ──────────────────────────────────────────────
    static constexpr int LCD_H_RES = 800;
    static constexpr int LCD_V_RES = 480;

    // Pixel clock. This unit showed artifacts at 16 MHz and 14 MHz; running at
    // the conservative 12.5 MHz used by esp32-smartdisplay. (~60 -> ~30 fps
    // panel refresh, not visible for a thermostat UI.)
    static constexpr int LCD_PIXEL_CLOCK_HZ = 12500000;

    // PCLK phase relative to the data lines (the parallel-bus analog of SPI
    // CPOL/CPHA). This unit shows occasional pixel artifacts with the
    // idle-low/falling-edge phase, so we run the opposite phase used by the
    // known-good LovyanGFX config: idle high, data latched on the rising edge,
    // which places the latch edge half a clock away from the data transitions.
    static constexpr bool LCD_PCLK_ACTIVE_NEG = false;
    static constexpr bool LCD_PCLK_IDLE_HIGH = true;

    // Sync timings from the LovyanGFX ESP32-8048S043 config (known good).
    static constexpr int LCD_HSYNC_PULSE_WIDTH = 4;
    static constexpr int LCD_HSYNC_BACK_PORCH  = 16;
    static constexpr int LCD_HSYNC_FRONT_PORCH = 8;
    static constexpr int LCD_VSYNC_PULSE_WIDTH = 4;
    static constexpr int LCD_VSYNC_BACK_PORCH  = 4;
    static constexpr int LCD_VSYNC_FRONT_PORCH = 4;

    // Control signals
    static constexpr int LCD_PIN_DE    = 40;
    static constexpr int LCD_PIN_VSYNC = 41;
    static constexpr int LCD_PIN_HSYNC = 39;
    static constexpr int LCD_PIN_PCLK  = 42;

    // Backlight (active high). Drive high to turn the panel on.
    static constexpr int LCD_PIN_BACKLIGHT = 2;

    // 16 RGB565 data lines, ordered as the esp_lcd RGB driver expects:
    //   data[0..4]  = B0..B4
    //   data[5..10] = G0..G5
    //   data[11..15]= R0..R4
    static constexpr int LCD_DATA_PINS[16] = {
        8, 3, 46, 9, 1,        // B0..B4
        5, 6, 7, 15, 16, 4,    // G0..G5
        45, 48, 47, 21, 14,    // R0..R4
    };

    // ── GT911 capacitive touch (I2C) ───────────────────────────────
    static constexpr int TOUCH_PIN_SDA = 19;
    static constexpr int TOUCH_PIN_SCL = 20;
    static constexpr int TOUCH_PIN_RST = -1;  // not wired on this board
    static constexpr int TOUCH_PIN_INT = -1;  // not wired on this board

    // The GT911 on (at least some batches of) this board ships with a factory
    // config for a 480x272 panel, so it reports touches in that coordinate
    // space even though the glass is 800x480 (measured maxima ~462x263).
    // Raw coordinates are scaled up to the panel resolution in Touch.h.
    static constexpr int TOUCH_RAW_X_MAX = 480;
    static constexpr int TOUCH_RAW_Y_MAX = 272;

    // Add further project-specific pin definitions below.
}
