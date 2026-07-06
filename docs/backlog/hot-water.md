# Domestic hot water (DHW)

**Step 1, item 6.** The demo never did DHW; the reference thermostat does.

- DHW enable/disable → DHW-enable bit in master status (ID 0), persisted.
- DHW setpoint (ID 56): 30–80 °C, default 60, persisted.
- DHW temperature readout (ID 26) + DHW-active status for the UI.
- Respect the boiler's DHW setpoint bounds (ID 48) if reported.

Done when: DHW toggle + setpoint reach the gateway/boiler and status reads
back on the UI.
