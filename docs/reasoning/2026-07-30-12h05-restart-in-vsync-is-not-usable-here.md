---
id: 2026-07-30-12h05
date: 2026-07-30
time: "12:05"
title: RESTART_IN_VSYNC is not usable on this board
supersedes:
---

`CONFIG_LCD_RGB_RESTART_IN_VSYNC` looks like the textbook cure for the vertical
slip we were chasing — IDF's own Kconfig help describes exactly our symptom
("stop permanent desyncs") — and it made things strictly worse: occasional shifts
became continuous ones. The reason is in the comment above
`lcd_rgb_panel_try_restart_transmission()`: re-arming the GDMA every VBlank turns
a *rare* permanent desync into a *per-frame* risk, because a restart that arrives
late re-sends bytes the LCD already clocked out. IDF assumes the ISR "has the
entirety of the VBlank time", and on the DIYLESS T3 it does not: that ISR shares
core 0 with the BT controller, NimBLE, WiFi and esp_timer, and the restart path
itself re-fills *both* bounce buffers (up to 19.2 KB of PSRAM memcpy) inside a
VBlank only 38 lines ≈ 1.98 ms wide. So the option is fine advice in general and
wrong for a board with a busy core 0 — worth recording, because the Kconfig text
will keep inviting whoever reads it next. Two related traps found while digging:
`CONFIG_LCD_RGB_ISR_IRAM_SAFE` cannot help while the framebuffer is in PSRAM,
since the bounce refill memcpys *out of PSRAM* and would crash with the cache
disabled (the driver says so in a comment); and the ESP32-S3 has **no LCD
underrun interrupt at all** (only the ESP32-P4 defines
`LCD_LL_EVENT_UNDERRUN`), so a desync is entirely silent and the absence of
"LCD underrun" in the log proves nothing. Rejected alongside it:
`SPIRAM_XIP_FROM_PSRAM`, which would have kept the cache alive across flash
writes — plausible, never tried, made moot once the bounce buffers went away
entirely (see [[2026-07-30-12h05-3]]).

Decision: keep `CONFIG_LCD_RGB_RESTART_IN_VSYNC` off, and record why in the
board's `sdkconfig.defaults` so it is not retried.
