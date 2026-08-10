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
// The STM32 link constants below are consumed by the opentherm-link feature;
// the LCD/touch constants by thermostat-ui. This board file is the single
// authoritative pin map for all of them.
// ──────────────────────────────────────────────────────────────

namespace BoardConfig
{
    // ── Shared I2C bus (GT911 touch + AHT20 sensor) ────────────────
    static constexpr int I2C_PIN_SDA = 17;
    static constexpr int I2C_PIN_SCL = 18;

    // ── GT911 capacitive touch (I2C, shared bus) ───────────────────
    // Address is auto-probed (0x5D default, 0x14 backup) in Touch.h; this unit
    // reports 0x14. INT drives an interrupt (esp_lvgl_port then runs the input
    // device in event mode); there is no ESP-controlled touch reset line.
    static constexpr int TOUCH_PIN_INT = 10;

    // ── RGB LCD panel (ST7701S) ────────────────────────────────────
    static constexpr int LCD_H_RES = 480;
    static constexpr int LCD_V_RES = 480;

    // Timing from diyless-thermostat-3.yaml, except the pixel clock.
    //
    // DIYLESS run this panel at 10 MHz. Over the 522x518 total below that is
    // ~37 Hz, so a rendered frame waits up to ~27 ms for the scanout to show it
    // — and once the touch INT and the LVGL refresh period were dealt with,
    // that swap was the largest thing left between a fingertip and a lit pixel.
    // 16 MHz makes it ~59 Hz and ~17 ms. PLL160M divides by exactly 10 at this
    // rate, so the clock stays jitter-free; 480x480x2 B at 59 Hz is ~27 MB/s of
    // PSRAM read, which octal PSRAM at 80 MHz does not notice.
    //
    // This is the one place we knowingly leave the vendor's numbers, so it is
    // also the first thing to put back if the panel ever misbehaves: the
    // failure mode is visual (tearing, shimmer, colour noise on a redraw), not
    // a crash, and it would show up here before anywhere else.
    static constexpr int LCD_PIXEL_CLOCK_HZ = 16000000;  // 16 MHz (vendor: 10)
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
    static constexpr int LCD_PIN_BACKLIGHT = 46;   // active-high, driven by LEDC

    // Backlight PWM. 1 kHz because that is what the board's own vendor config
    // uses (DIYLESS's ESPHome yaml declares a bare `platform: ledc`, whose
    // default is 1 kHz) and it demonstrably dims this hardware. Do not raise it:
    // at 20 kHz the LED driver switched off entirely below full duty instead of
    // dimming — its enable input cannot follow that fast.
    static constexpr int LCD_BACKLIGHT_PWM_HZ = 1000;

    // 3-wire SPI used only for the ST7701 init sequence (all direct GPIO).
    static constexpr int LCD_SPI_CS  = 1;
    static constexpr int LCD_SPI_SCK = 12;
    static constexpr int LCD_SPI_SDA = 11;

    // ── OpenTherm via STM32L051 co-processor (UART link) ───────────
    // The STM32 owns the OT PHY; the ESP drives it over a UART at 200000 baud
    // using the DIYLESS STM32 app-protocol (see Stm32OpenThermLink.h). These
    // pins are the same GPIO11/12 used for the ST7701 3-wire-SPI init above —
    // the panel only needs them during bring-up, after which they carry the
    // STM32 link (the DIYLESS ESPHome config marks them allow_other_uses).
    //   ESP TX → STM32 = GPIO12 (= LCD_SPI_SCK),  ESP RX ← STM32 = GPIO11 (= LCD_SPI_SDA)
    // BOOT0 low + a pulse on NRST (active-low, normal polarity) makes the STM32
    // run its flash application. NOTE: TX/RX are the reverse of the ESPHome
    // in_pin/out_pin names — verified empirically (a valid CpuStatusResponse
    // comes back only with TX=12/RX=11). The ESPHome names are from the OT-PHY
    // side, not the ESP UART side.
    static constexpr int OT_UART_TX     = 12;  // ESPHome opentherm in_pin
    static constexpr int OT_UART_RX     = 11;  // ESPHome opentherm out_pin
    static constexpr int OT_STM32_BOOT0 = 44;  // ESPHome opentherm boot_pin
    static constexpr int OT_STM32_NRST  = 13;  // ESPHome opentherm reset_pin

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
