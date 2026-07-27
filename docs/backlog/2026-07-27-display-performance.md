# Display performance — sluggish touch, tearing and jumping

**Status: reported, not investigated.** (logged 2026-07-27, Bas) Distinct from
`2026-07-09-ui-visual-polish.md`, which is about how it *looks*; this is about how
it *behaves*.

## Symptoms

- Touch feels **sluggish** — noticeable lag between press and response.
- **Tearing** and **jumping** when the screen updates.

## Current configuration (all in `DisplayManager::InitLvgl`)

- Draw buffer **480 × 20 lines**, `double_buffer = false`, in **internal DMA RAM**.
- RGB panel in **bounce-buffer mode** (`bb_mode = true`), `avoid_tearing = false`.
- LVGL task: 8192 stack, pinned to **core 1** (core 0 left for WiFi).
- Screens refresh on their own `lv_timer`s (home 1 s, WiFi 500 ms) and are built
  once on first load.

## Why the obvious fix is not obvious

Rendering into a **PSRAM** draw buffer was already tried and rejected: it competes
with the panel DMA for PSRAM bandwidth and produces on-screen artifacts. Internal
RAM + bounce buffer is the currently-proven config, and that comment in
`InitLvgl()` is load-bearing — don't "fix" it by moving the buffer back to PSRAM
without re-testing on hardware.

## Candidate directions (untested)

- **Tearing** is expected with `avoid_tearing = false` and a single partial buffer:
  a 480×480 RGB panel scans continuously while LVGL writes into it. The supported
  cure is `avoid_tearing` with two full framebuffers plus the bounce buffer — which
  is exactly the PSRAM-bandwidth trade-off above, so it needs measuring, not
  assuming.
- **Jumping** may be the same cause, or a 20-line buffer meaning ~24 flushes per
  full redraw.
- **Touch latency** is a separate path: GT911 poll interval, the `esp_lvgl_port`
  task period / `LV_DEF_REFR_PERIOD`, and I2C clock speed. Worth measuring before
  touching the display pipeline — it may be entirely unrelated to tearing.
- Check whether a screen's refresh timer does more work than it needs to
  (`WifiScreen` rebuilds its list on scan completion; the rest only set label text).

## Relations

- Pairs with `2026-07-09-ui-visual-polish.md` (looks). Decide whether both are done
  in one pass, since both touch the same code.
- Hardware quirks for this panel are recorded in the ESP32-8048S043 / DIYLESS
  display notes — the artifact-vs-bandwidth trade-off has bitten before.
