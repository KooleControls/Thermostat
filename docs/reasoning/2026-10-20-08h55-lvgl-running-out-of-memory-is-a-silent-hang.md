---
id: 2026-10-20-08h55
date: 2026-10-20
time: "08:55"
title: LVGL running out of memory is a silent hang, from a pool nobody was counting
builds-on: 2026-07-30-12h05-3
supersedes:
---

**Before:** the display's memory story was the one in 2026-07-30-12h05-3 — PSRAM
framebuffers, no internal draw buffer, internal DRAM measured with BLE up. LVGL's
own allocator was not part of that budget: its 64 KB built-in pool (the Kconfig
default, `LV_USE_BUILTIN_MALLOC`) had never been chosen or sized. And an LVGL
allocation failure was assumed to look like a failure — an error, a crash, a reboot.

**What changed it:** the display froze right after a WiFi scan. The task watchdog
fired on IDLE1 every 5 s with `taskLVGL` on CPU 1 parked at **the same PC in every
sample** (`circ_calc_aa4`, drawing a rounded rectangle). An identical PC across
samples taken seconds apart rules out slow drawing; it is a spin. `lv_malloc` had
returned NULL, `LV_ASSERT_MALLOC` fired, and the default `LV_ASSERT_HANDLER` is
`while(1);`. With `LV_USE_LOG` off nothing was printed, and with
`ESP_TASK_WDT_PANIC` off nothing rebooted. The allocation that failed was a few
hundred bytes; it was simply the last one. Everything LVGL owns comes from that one
pool — objects and their local styles, label text, draw tasks, mask and layer
buffers — while hidden screens stay built, the WiFi screen carries a whole
keyboard, and a rescan adds a card and two labels per network.

**Now:** LVGL running out of memory is not an error path but a hang, and under the
defaults it is indistinguishable from a busy render loop except by that unmoving
PC. The pool was a second, fixed memory budget sitting outside the one we had
measured, and the UI's growth had been spending it with no signal. Routing LVGL
through the IDF heap (`LV_USE_CLIB_MALLOC`) dissolves the separate budget rather
than enlarging it: the 2 KB `SPIRAM_MALLOC_ALWAYSINTERNAL` threshold keeps small
allocations internal and sends layer buffers to PSRAM, and the 64 KB static
reservation leaves `.bss` entirely. LVGL's log at WARN makes the next assert name
its expression on the serial console.

Rests on: LVGL's small allocations together staying well under the 64 KB they used
to have reserved. Still open, and the real cost of this change: LVGL is no longer
*bounded* in internal DRAM. It now competes at runtime with BLE, whose controller
asserts on its own internal malloc failure (2026-07-29-14h36-2), while
`BleManager`'s DRAM floor is checked only at start. Not yet measured: internal heap
free after a long session that visits every screen.

**Follows:** `LV_USE_CLIB_MALLOC` and `LV_USE_LOG` (WARN, printf) in the root
`sdkconfig.defaults`.
