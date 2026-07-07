# UI driven through the command layer (shared screen + web)

**Status: idea, parked.** (Bas, 2026-07-07, during thermostat-ui item 7.)

Idea: have the on-screen UI do everything it needs **through the device
command surface** (`climateSet`/`hotWaterSet`/…), rather than calling manager
methods directly. Then the same UI could be **recreated on the web page** —
the browser drives the identical commands, so screen and web share one control
path (and, with LVGL's web/emscripten build, potentially the same LVGL UI code
rendered in the browser).

## Why it's attractive

- One control path → screen and web can't drift. Every action is a command,
  already the device's tested RPC surface.
- Pairs naturally with `command-page.md` (a generic command UI) and
  `web-integration.md` (item 8, the web climate/DHW page).
- LVGL can target the browser (WASM), so in principle the exact screen layout
  could be reused on the web, not just reimplemented.

## Why it's parked (not how item 7 is being built)

- Item 7 ships the minimal screen calling `ClimateManager::NudgeSetpoint()` /
  `GetUserSetpoint()` **directly** — simplest path to first-light, no
  command-plumbing detour. Approved that way.
- Going command-driven touches the command layer's shape (in-process command
  invocation from the UI task, reply handling) and overlaps the undecided
  KC-command rework (`kc-command-tunnel.md`) and `command-page.md` — better
  designed once those are settled.

## Revisit when

- Doing `web-integration` (item 8): decide then whether the web page and the
  screen share a command-driven core, and whether to retrofit the screen's
  direct manager calls to go through commands. See [[thermostat-ui-minimal]] and
  the command-surface backlog items.
