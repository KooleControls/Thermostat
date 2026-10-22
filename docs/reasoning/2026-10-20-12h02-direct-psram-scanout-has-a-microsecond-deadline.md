---
id: 2026-10-20-12h02
date: 2026-10-20
time: "12:02"
title: Streaming the panel straight out of PSRAM has a deadline of microseconds, and flash traffic misses it
builds-on: 2026-07-30-12h05-3
supersedes:
---

**Before:** two framebuffers in PSRAM with the GDMA streaming straight from
them (2026-07-30-12h05-3) was understood to have *no* scanout deadline: the
bounce-buffer refill ISR was the deadline that produced the vertical slip, and
removing the bounce buffers removed it. The sideways shift that remained was
put down to flash writes turning the cache off, and `SPIRAM_XIP_FROM_PSRAM`
(2026-09-21) was recorded in the board's `sdkconfig.defaults` as removing that
cause, "so the GDMA keeps reading through it and never under-runs".

**What changed it:** the shift came back with XIP on, and reproduced on every
pairing, at the moment NimBLE stores the bond — while four plain settings
writes a few seconds earlier never shifted it. IDF's `rgb_lcd.rst` says what
XIP does not fix: on the S3, PSRAM "shares the same SPI bus with the main
flash. At any given time, there can only be one consumer". XIP keeps the cache
on during a flash write; it does not give the bus back. And a bond store is a
burst, not a write: NimBLE reads every bond slot (two `nvs_get_blob` each, up to
MAX_BONDS) before each blob it writes, straight after ~2 s of pairing crypto
running out of PSRAM through a 16 KB I-cache. A polled GDMA `OUTFIFO_UDF` bit
never fired across a shift, which the same document explains: the LCD clocks
out dummy bytes while the DMA waits, so the DMA side never underflows and the
event is invisible. Ruled out on the way: the MSPI low-speed switch, which on
the S3 happens only when the CPU moves to XTAL (DFS, light sleep) — neither is
active here.

**Now:** direct scanout does have a deadline — the GDMA FIFO, a few
microseconds of pixels at 10 MHz — and it is hidden, so it was counted as none.
Any burst on the shared bus can outlast it. 10-line bounce buffers in internal
RAM, under the same two framebuffers, move the GDMA off the shared bus and turn
the deadline into ~500 us; LVGL still renders into PSRAM in one pass, so the
render-cost finding of 2026-07-30-12h05-3 stands. On the bench the pairing shift
is gone. The bounce-buffer vertical slip that once argued against this predates
XIP, when every flash write stopped the refill outright — the likely cause, not
a confirmed one; if a refill is missed now, the driver detects it and restarts
at VSYNC, one frame rather than a permanent shift. XIP stays, as what the
refill needs. BLE still starts with the 19 KB of internal RAM gone — it paired
on the bench — but its margin above the 48 KB floor is unmeasured, and so is
the CPU cost of the refill memcpy on core 0.

**Follows:** `bounce_buffer_lines = 10` with `num_fbs = 2` and `bb_mode`; the
board comment that credited XIP corrected. The blind `esp_lcd_rgb_panel_restart`
after screen changes, added the same day as a workaround, is now redundant.
