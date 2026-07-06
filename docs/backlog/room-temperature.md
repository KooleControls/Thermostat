# Room temperature sensing

**Step 1, item 4.** Own the "measured room temperature" contract that feeds
the PID and the UI.

- AHT20 sampling via the board's ambient-sensor role.
- Calibration offset setting (TypedSettings): the T3 board self-heats ≈ +7 °C
  (RA2-389 tracks vendor guidance; DIYLESS's own config reads it raw and
  calls it "Built In Sensor Temperature" for a reason).
- Sensor-failure handling: stale/invalid reading → no demand (safe state),
  visible fault.
- Keep the source abstract enough that an external/BLE sensor can slot in
  later (Step 2) without touching consumers.

Done when: offset-corrected room temp is available to ClimateManager/UI and
a disconnected sensor degrades safely.
