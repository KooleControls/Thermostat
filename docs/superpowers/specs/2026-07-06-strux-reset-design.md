# Strux Reset — Design

**Date:** 2026-07-06
**Status:** Approved
**Repo:** KooleControls/Thermostat

## Problem

The Thermostat project was forked from the Strux template, but Strux has moved
97 commits ahead (settings restructure, SystemManager, web-UI auth/sessions,
new lib/ and hardware/interfaces layers). The thermostat side carries 23
commits of quick-and-dirty demo work (Climate/PID, OpenTherm manager, display
UI, BLE link) that we do not want to preserve in place. Merging the two is
possible but pointless: the demo capabilities are disposable, and features
will be rebuilt deliberately, one by one, each through its own
brainstorm → spec → plan → implement cycle.

## Decision

Reset the Thermostat repo to **pure Strux** — byte-for-byte current
`strux/main` — and rebuild everything thermostat-specific as future features.

Considered and rejected:

- **Git merge from strux/main** (11 conflicts, preserves demo work) — rejected
  because the demo work is explicitly disposable.
- **Strux + carry-over of DIYLESS board/drivers** — rejected in favor of a
  fully clean slate; hardware support returns as the first feature.

## Plan of record

1. **Branch:** create `feature/strux-reset` in the Thermostat repo pointing
   exactly at `strux/main` (the repos share history; Strux is added as local
   remote `strux`). No thermostat content on it. Existing work remains
   untouched on `feature/ot-thermostat-dropin` for reference/mining.
2. **Spec:** this document is committed on the new branch under
   `docs/superpowers/specs/`.
3. **Verify:** frontend `pnpm build` succeeds; `idf.py build` succeeds,
   exactly as Strux builds today (Strux default target/board). No hardware
   test — there is no thermostat code to test.
4. **Push:** `feature/strux-reset` to `origin` (KooleControls/Thermostat).
   `main` is not touched; promoting the branch is a later, explicit decision.

## Explicit consequences (accepted)

- The device identifies as **Strux** (AP SSID, MQTT prefix, web UI title)
  until a branding/identity feature restores "Thermostat".
- Firmware targets Strux's default board (plain ESP32 devkit); it does not
  run on the DIYLESS T3 until board support lands as a feature.
- The display stack, OpenTherm link, and climate logic are absent until their
  features are rebuilt.

## Future feature backlog (each its own spec/plan cycle; order indicative)

1. DIYLESS T3 board support — board config, ST7701 panel / GT911 touch /
   AHT20 drivers, ESP32-S3 target + PSRAM sdkconfig (mine
   `feature/ot-thermostat-dropin`).
2. OpenTherm master link — `Stm32OpenThermLink` driver + manager
   (DIYLESS STM32 nibble protocol, TX12/RX11, 900 ms warm-up).
3. Display/UI (LVGL).
4. Climate control logic (setpoint/PID, heating demand).
5. Branding/identity ("Thermostat" naming).

## Verification / acceptance

- `git diff feature/strux-reset strux/main` shows only files under
  `docs/superpowers/` (this spec and its implementation plan).
- `pnpm build` and `idf.py build` complete without errors on the new branch.
- Branch pushed to origin.
