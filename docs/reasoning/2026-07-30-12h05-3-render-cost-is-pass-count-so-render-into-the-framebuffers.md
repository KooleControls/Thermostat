---
id: 2026-07-30-12h05-3
date: 2026-07-30
time: "12:05"
title: Render cost is pass count, so render straight into the framebuffers
supersedes: 2026-07-29-14h36-2
---

Measuring instead of guessing produced a scaling law that settled the display
architecture: a full-screen redraw cost **108 ms of render with a 10-line draw
buffer and 54 ms with 20 lines**, so doubling the buffer halved the time —
per-*pass* overhead dominated, not pixel work, because 480×480 in 10-line slices
is 48 passes each re-walking the object tree and re-clipping every object. The
endpoint of that law is one pass, which is direct mode: **two PSRAM framebuffers,
LVGL rendering into them, swapping on VSYNC** (`num_fbs = 2`,
`bounce_buffer_lines = 0`, `avoid_tearing`, `direct_mode`). Result 47 ms render and
12 ms flush, but the number that mattered was FPS *during* a screen change —
10–16 before, 19–26 after, against a 26–30 idle, i.e. the panel stopped halving
its refresh rate whenever the menu opened. Two side effects outweigh the speed:
the staging copy into the framebuffer disappears, and with no bounce buffers there
is no GDMA EOF interrupt every 480 µs doing a CPU memcpy out of PSRAM under a hard
deadline — the mechanism behind the vertical slip stops *existing* rather than
being made less likely (compare [[2026-07-30-12h05]], where making it less likely
backfired). This **supersedes the DRAM premise of 2026-07-29-14h36-2** in two
places: the LVGL draw buffer is no longer "the 9.6 KB lever" because there is no
draw buffer at all, and internal DRAM is no longer the scarce resource — measured
21.8 KB → 48.7 KB free after the BLE stack, a gain of ~27 KB, since the draw
buffer *and* the bounce buffers both went to PSRAM. That note's claim that halving
the buffer cost "only smaller render batches" was also too generous: it cost 2×
render time, invisible until sysmon was switched on. Its WiFi-buffer trade still
stands and is untouched. Also supersedes the belief, previously only a code
comment in `InitLvgl`, that PSRAM draw buffers cause artifacts — that observation
was real but incomplete, made without double buffering or a VSYNC-synced swap,
which are exactly what stop the renderer and the scanout touching one buffer.
Rejected: simply enlarging the internal draw buffer, which works (54 ms) but cut
BLE's headroom to 12.5 KB free / 7.7 KB largest, too thin given NimBLE's silent
4 KB stack failure; and `-O2` on its own, which is real (≈2× render) yet
imperceptible because render only happens on a genuine change. Known cost: touch-
to-visible latency now includes the VSYNC swap (~40–65 ms total), and the LVGL
task is still priority 4 while every app task is 5.

Decision: two PSRAM framebuffers with LVGL in direct mode; no bounce buffers, no
internal draw buffer.
