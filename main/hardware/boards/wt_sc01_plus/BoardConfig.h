#pragma once

// ──────────────────────────────────────────────────────────────
// Board configuration — hardware-specific pin assignments and constants.
// ──────────────────────────────────────────────────────────────

namespace BoardConfig
{
    // ──────────────────────────────────────────────────────────────
    // Board: Wireless-Tag WT-SC01 Plus
    //   ESP32-S3-WROOM-1 (N16R8: 16 MB flash, 8 MB octal PSRAM)
    //   3.5" 320x480 IPS, ST7796(UI) controller over an 8-bit i80/8080 bus
    //   FT6336U capacitive touch over I2C (FocalTech FT5x06 family)
    //   No rotary knob.
    //
    // Pin map is the canonical WT-SC01 Plus assignment (matches the widely
    // shared LovyanGFX config). This is the first command-driven panel in the
    // tree — the other three boards are RGB-direct-framebuffer — so it goes
    // through the non-RGB LVGL path in DisplayManager (see BOARD_PANEL_RGB).
    // ──────────────────────────────────────────────────────────────

    // LED — no separate user LED; the LCD backlight is on GPIO45.
    static constexpr int LED_PIN = -1;
    static constexpr bool LED_ACTIVE_HIGH = true;

    // ── ST7796 LCD panel (8-bit i80/8080 parallel) ─────────────────
    // Panel is native portrait 320x480; presented as LANDSCAPE 480x320.
    // The rotation is driven by esp_lvgl_port (disp_cfg.rotation.swap_xy in
    // DisplayManager), which is the component that owns the panel's
    // swap_xy/mirror state — NOT a manual esp_lcd_panel_swap_xy() here (the
    // port would override that). These are the logical landscape dimensions.
    static constexpr int LCD_H_RES = 480;
    static constexpr int LCD_V_RES = 320;

    // Bits per pixel on the wire (RGB565). The 8-bit bus sends 2 bytes/pixel.
    static constexpr int LCD_BITS_PER_PIXEL = 16;

    // i80 pixel-clock (WR strobe). ST7796 over an 8-bit bus is comfortable at
    // 20 MHz; raise during bring-up if the panel keeps up.
    static constexpr int LCD_PIXEL_CLOCK_HZ = 20000000;

    // Control signals
    static constexpr int LCD_PIN_DC   = 0;    // data/command (RS)
    static constexpr int LCD_PIN_WR   = 47;   // write strobe (i80 PCLK)
    static constexpr int LCD_PIN_CS   = -1;   // tied active on this board
    static constexpr int LCD_PIN_RST  = 4;    // panel hardware reset

    // Backlight (active high). Drive high to turn the panel on.
    static constexpr int LCD_PIN_BACKLIGHT = 45;

    // 8 data lines D0..D7.
    static constexpr int LCD_DATA_PINS[8] = {
        9, 46, 3, 8, 18, 17, 16, 15,
    };

    // Number of scanlines per LVGL flush buffer; also sizes the i80 bus max
    // transfer. Two buffers of this height are allocated in internal DMA RAM.
    static constexpr int LCD_DRAW_BUFFER_LINES = 40;

    // ── FT6336U capacitive touch (I2C, FocalTech FT5x06 family) ─────
    static constexpr int TOUCH_PIN_SDA = 6;
    static constexpr int TOUCH_PIN_SCL = 5;
    static constexpr int TOUCH_PIN_RST = -1;  // not wired on this board
    static constexpr int TOUCH_PIN_INT = 7;
}
