# Climate control (PID)

**Step 1, item 5.** `ClimateManager`: the thermostat owns setpoint, mode and
the control loop.

- Setpoint (~5–30 °C, 0.5 ° steps) + mode **Heat/Cool/Off**, NVS-persisted.
  Frost-safe: Off keeps a minimum setpoint floor.
- Heating: clean-room PID → `t_set` 30–80 °C, zero-means-zero.
  **ESPHome is GPLv3 — reuse the algorithm and constants, never the source.**
  Constants from DIYLESS `diyless-thermostat-3.yaml`: kp 0.77, ki 0.0005,
  kd 0, 10-sample output averaging; deadband ±0.5 °C with ki×0.15 and
  15-sample averaging inside the band.
- Cooling: on/off demand (cooling-enable bit) when room temp exceeds
  setpoint + deadband; only offered if the slave config advertises cooling.
  Actuation/modulation stays the gateway's job.
- CH-enable driven by mode + demand; respect boiler max-t_set bounds.

Done when: setpoint step on the bench produces a plausible modulating t_set
curve (heating) and a cooling request in Cool mode.

**Explicit deliverable — OpenThermManager slims to pure transport:** when
ClimateManager takes ownership of setpoint/mode/room temp, remove the
manager's temporary feeds (direct `GetTemperatureSensor()` read for ID 24,
demand defaults) and reassess its size — the deferred responsibility-split
question (Bas, 2026-07-07) gets answered here, not before.
