---
id: 2026-10-22-16h08
date: 2026-10-22
time: "16:08"
title: The screen stayed dark for the OpenTherm co-processor, not for the panel
builds-on:
supersedes:
---

**Before:** the slow start-up — about 2.6 s of dark panel after power-on — was
taken to be what a 480x480 RGB panel with LVGL costs to bring up.

**What changed it:** a boot log with timestamps, taken over USB on a unit:

| ms after power-on | |
|---|---|
| 0–399 | ROM and bootloader, which checks and loads the 1.9 MB app |
| 399–598 | code and read-only data copied into PSRAM (`SPIRAM_XIP_FROM_PSRAM`) |
| 598–918 | PSRAM memory test (`CONFIG_SPIRAM_MEMTEST`), 320 ms |
| 926–1001 | app starts; console, settings, WiFi AP, time, commands |
| 1001–2575 | `Board::Init`, 1.57 s. Mostly the STM32 OpenTherm co-processor's reset and ~0.9 s warm-up, plus the AHT20 settle |
| 2645 | DisplayManager's first frame, backlight on |

The panel itself was ready early in `Board::Init`. It stayed dark because the
backlight waited for LVGL, and LVGL waited behind every manager that `main.cpp`
starts before it.

**Now:** the dark screen at boot is an ordering artefact, not a display cost.
The panel can be lit within a few hundred ms of the app starting, by splitting
the display out of `Board::Init` and drawing into its framebuffers before LVGL
exists. What still lies before that point is fixed cost:
- the bootloader;
- the copy into PSRAM, which the bounce buffers depend on;
- the PSRAM memory test, the one part that is a choice: 320 ms against
  catching bad PSRAM at boot.

The co-processor's warm-up still delays the real UI. It is now behind the logo
rather than behind a dark screen.

**Follows:** `Board::InitDisplay` + `DisplayManager::ShowBootLogo` run right
after the console in `app_main`, and LVGL's first frame takes over from the logo.
