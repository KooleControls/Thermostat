# Room temperature calibration

**Status: deferred, needs its own brainstorm.** Split out of
`room-temperature` (item 4) on 2026-07-07 — the raw measurement contract
shipped without any correction.

The T3 board self-heats (≈ +7 °C observed; RA2-389 tracks vendor guidance —
DIYLESS's own config reads the AHT20 raw and calls it "Built In Sensor
Temperature" for a reason). A correction is clearly needed before the PID
can hold a room at an honest setpoint.

## Open questions (the brainstorm)

- **Offset vs curve:** self-heating varies with electrical load (display
  brightness, WiFi, CPU) — is a constant offset good enough, or do we need
  a load-dependent correction (e.g. offset as a function of backlight duty)
  or a warm-up model (board heats over ~30 min after boot)?
- **How to calibrate:** manual user offset (classic thermostat menu item),
  guided calibration against a reference thermometer, or factory default
  per board revision + user trim?
- **Where it lives:** applied in `RoomTemperatureManager` (single source of
  corrected truth — consumers stay ignorant), configured via TypedSettings.
  That part is settled; the correction *model* is not.
- Vendor input pending in RA2-389.
