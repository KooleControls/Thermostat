# Backlight dimming (reduce self-heating)

**Status: not started.** (logged 2026-07-27) Dim (and time out) the display
backlight so the board dissipates less power and **self-heats less**. The primary
motive is temperature accuracy, not UX: the panel/backlight is a big contributor
to the ~+7 °C self-heating that skews the AHT20 room reading (see RA2-389 and the
calibration question in RA2-442). Less heat at the sensor → a smaller, more stable
calibration offset. Screen timeout / power saving is a welcome side benefit.

The measurement half of this is now built and documented separately in
`2026-07-28-self-heating-test-protocol.md` — `ThermalTestManager` already owns
PWM brightness, the panel refresh clock and the CPU clock as test levers. What
is left here is the *shipping* behaviour: idle timeout, restore on touch, and
the settings that expose it.

## Likely scope

- ~~**Backlight brightness control**~~ — done: the board drives the backlight
  with LEDC (`Board::SetBacklightPercent`). `DisplayManager` still only ever
  sets 100 %.
- **Dim / off after inactivity** — a configurable idle timeout that lowers
  brightness (and optionally turns the backlight off); restore to full on touch.
- **Settings** — brightness level(s) and timeout as persisted settings (typed
  NVS), so it's tunable without a rebuild.
- **Interaction with the sensor** — since the goal is thermal, consider how much
  the dim state actually reduces sensor error; the calibration decision (RA2-442)
  should account for whichever backlight behaviour we ship (offset measured in the
  steady dimmed state, not at full brightness).

## Relations

- Feeds the **room-temp calibration** open question (RA2-442 #2) — dimming changes
  the self-heating the offset has to correct for; decide them consistently.
- Part of the on-device display work; fits with `2026-07-09-ui-visual-polish.md`
  (screen timeout / backlight were deferred from the minimal first-light screen).
- `DisplayManager` owns the backlight; keep the control surface generic (boundary
  rule).
