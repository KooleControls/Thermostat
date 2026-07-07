# Room temperature sensing

**Step 1, item 4.** Own the "measured room temperature" contract that feeds
the PID and the UI. Spec: `docs/superpowers/specs/2026-07-07-room-temperature-design.md`.

- AHT20 sampling via the board's ambient-sensor role (own task, 5 s cadence).
- Sensor-failure handling: stale reading (> 30 s) → contract reports invalid,
  fault logged once. (Acting on it — dropping demand — is `climate-pid`'s job.)
- Keep the source abstract enough that an external/BLE sensor can slot in
  later (Step 2) without touching consumers: single `GetRoomTemperature()`
  contract on `RoomTemperatureManager`.
- **Calibration moved out** → `room-temp-calibration.md` (offset vs curve
  undecided, RA2-389); **humidity moved out** → joins `thermostat-ui`.

Done when: room temp with validity signal is available to consumers and a
dead sensor degrades to a visible fault instead of a stale value.
