# Web UI integration

**Step 1, item 8.** (2026-07-06) Expose climate/DHW/diagnostics through Strux's existing
WebSocket command plumbing (no MQTT/HA — removed; the gateway owns
smart-home integration).

- Command tables (`CommandEntry`) on ClimateManager / OpenThermManager for
  get/set of setpoint, mode, DHW; a status/diagnostics read-out command.
- Frontend: a thermostat page (setpoint, mode, DHW) + a diagnostics view
  (boiler sensors, fault codes, OT link state).
- Settings (offset, clamps, timeouts) come for free via TypedSettings +
  the generated settings UI.

Done when: full control + diagnostics from the browser, live-updating.
