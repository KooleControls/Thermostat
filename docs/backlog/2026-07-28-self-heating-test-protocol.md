# Self-heating measurement — the rig and how to run it

**Status: rig built (2026-07-28), measurement not yet run.** The firmware side is
in place (`ThermalTestManager`, web UI *Thermal* page, and `tools/thermal_log.py`
in the surrounding workspace — next to `thermostat_ws.py`, outside this repo);
what remains is the wall-mounted run and the conclusion it feeds into.

## Why

The AHT20 shares an enclosure with a backlit 4" panel, a continuously scanning
RGB refresh path and a 240 MHz SoC, and reads roughly **+7 °C** above the actual
room (RA2-389). That is far too much to paper over with a software offset: the
self-heating rise scales with the difference to ambient, so a single constant
will not hold across seasons. The point of this exercise is to get the residual
error small enough that a constant *becomes* legitimate — or to establish that it
can't be, which makes it a sensor-placement problem instead.

Feeds the calibration decision in RA2-442 #2 and the shipping dim-on-idle
behaviour in `2026-07-27-backlight-dimming.md`.

## The levers, and what was rejected

Ranked by expected effect:

| Mode | Levers | Notes |
|---|---|---|
| `baseline` | backlight 100 %, pclk 10 MHz, 240 MHz | as shipped |
| `dark` | backlight 0 % | expected to be most of the win |
| `panelidle` | + pclk 1 MHz | cuts the DMA/PSRAM traffic scanning the framebuffer |
| `lowpower` | + CPU 80 MHz | smallest lever; watch whether OT survives it |

**Light sleep is not on the table.** The RGB panel needs a continuously
scanning DMA, the OT master needs its UART timing, and the unit must stay
reachable over WiFi — so `CONFIG_FREERTOS_USE_TICKLESS_IDLE` stays off.
`CONFIG_PM_ENABLE` is on solely so `esp_pm_configure` can pin the CPU clock; the
rig always sets `max == min`, so DFS never runs either.

**The panel cannot be switched off.** On this board the ST7701's 3-wire SPI
lines are reused as the STM32 OpenTherm UART, and with no `disp_gpio` wired the
esp_lcd_st7701 driver implements `esp_lcd_panel_disp_on_off()` by sending
DISPOFF over exactly those pins — it would corrupt the OT link. Slowing the
refresh clock is the available proxy. Caveat: a large pclk change can leave the
panel out of sync until a reboot; harmless while the backlight is off, but it is
why `panelidle`/`lowpower` are manual choices rather than the default cycle.

## Protocol

1. Mount the unit as production: in its enclosure, flush to the wall, no draft,
   no sun.
2. Put a **reference logger ~10 cm away**, same wall, same height. Required for
   the absolute residual — the on-board sensor alone can only give differences.
   (A second AHT20 cannot go on the internal bus: fixed address 0x38.)
3. In the web UI's *Thermal* page: cycle **on**, A = `baseline`, B = `dark`,
   dwell **90 min** (~3× the enclosure's thermal time constant).
4. Start `python thermal_log.py --host <ip> --out selfheat.csv` (workspace
   `tools/`) and leave it. It backfills from the device's ring buffer after any
   dropout.
5. Let it run overnight at least — several full A/B rounds, not one.
6. Analysis: per dwell, take only the **last 20 minutes** as the steady-state
   value. Compare A against B within each round, so ambient drift cancels.
7. Repeat with A = `dark`, B = `panelidle` (and then `lowpower`) to price the
   remaining levers.

Optional, and worth doing once the dim state is characterised: the logger's
`--wake-every 15 --wake-for 30` pulses the backlight briefly. If a 30 s
interaction barely moves the settled reading, calibrating for the dimmed state
is sound.

## Decision rule

Residual after all levers, against the reference:

- **≤ ~0.3 °C and stable** → ship with no offset.
- **1–2 °C but repeatable** → a fixed offset is defensible.
- **still varying with ambient** → software can't fix it; the answer is sensor
  placement (vented, thermally decoupled) or an offset modelled on dissipated
  power. Record which one and why.

## Notes for whoever runs it

- Mode and cycle settings persist in NVS, so an unattended run survives a
  reboot. A restored `dark` mode means the unit comes up with a dark panel on
  purpose — the boot log says so.
- Touching the screen deliberately does **not** wake the backlight; a passing
  hand must not perturb a measurement.
- Every sample is one CSV line on the console prefixed `THERMAL`, so a plain
  `idf.py monitor` capture is also a complete data file.
- Humidity is logged as a second, independent witness: a self-heated sensor
  reads RH low.
