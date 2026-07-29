# Self-heating — measuring it against a reference gateway

**Status: firmware side built (2026-07-28), measurement not yet run.**

## Why

The room sensor shares an enclosure with a backlit 4" panel, a continuously
scanning RGB refresh path and a 240 MHz SoC, and reads roughly **+7 °C** above
the actual room (RA2-389). That figure is not cosmetic: the same float goes to
the display, to the PID, and out over OpenTherm as Troom, so the gateway acts on
it. Too much to paper over with a constant, since the rise scales with the
difference to ambient — the question is how far the hardware can be quieted
before deciding anything about calibration (RA2-442 #2).

## How it is measured

**Two gateways, side by side.** The unit under test reports Troom over
OpenTherm to one gateway; a second gateway with the third-party thermostat is
the reference. Both gateways log; the difference between the two logs is the
answer.

That is deliberately the whole measurement rig. The thermostat records nothing
and needs no reference sensor of its own — which is also why **every power state
below keeps the OpenTherm link alive.** A mode that stopped OT (light sleep,
radio off) would take the gateway's log down with it and measure nothing, so
none is offered.

## Power states (web UI → Thermal)

| Mode | Levers |
|---|---|
| Baseline | as shipped — backlight 100 %, pclk 10 MHz, 240 MHz |
| Dark screen | backlight 0 % |
| Panel idle | + refresh clock at 1 MHz |
| Panel off | + ST7701 held in reset, CPU 80 MHz — **one-way, reboot to restore** |

Plus a brightness row (0/25/50/100 %) for finding the dimmest setting still
readable on a wall.

Nothing persists: a reboot lands in Baseline with a lit screen, which is both
the safe state and an obvious step in the gateway's log.

## Two hardware facts behind the modes

**The panel cannot be switched off through esp_lcd.** On this board the ST7701's
3-wire SPI lines are reused as the STM32 OpenTherm UART, and with no `disp_gpio`
wired the esp_lcd_st7701 driver implements `esp_lcd_panel_disp_on_off()` by
sending DISPOFF over exactly those pins — it would corrupt the OT link. So
"panel idle" slows the refresh clock instead (proportionally less DMA/PSRAM
traffic), and "panel off" holds the panel in **hardware reset** via GPIO43. There
is no panel power-enable line. Both are one-way in practice: a large pclk step
can leave the panel out of sync, and the reset hold needs an init sequence we
can no longer send.

**The CPU clock needs `CONFIG_PM_ENABLE`**, which is on for that reason alone.
Light sleep stays off — the RGB panel needs a continuously scanning DMA and the
OT master needs its UART timing. The rig pins `max == min`, so DFS never runs.

## Running it

Give each state long enough to settle — the enclosure's time constant is tens of
minutes, so ~90 min per state, and read only the tail. Switch state from the
Thermal page, note the time, and let the two gateway logs do the rest.

Die temperature is on the Thermal page next to the room reading. It is the quick
check that a mode actually removed heat, before waiting an hour to see it in the
room figure.

## Decision rule

From the difference between the two gateways' logs, once quiet:

- **≤ ~0.3 °C and stable** → ship with no offset.
- **1–2 °C but repeatable** → a fixed offset is defensible.
- **still varying with ambient** → software can't fix it; the answer is sensor
  placement (vented, thermally decoupled, or external). Record which and why.
