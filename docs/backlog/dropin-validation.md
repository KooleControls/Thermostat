# Drop-in validation against the gateway

**Step 1, item 9 (exit criterion).** (2026-07-06) Prove the thermostat is a 1:1
replacement for the third-party unit — no gateway changes (THR=1).

- Heat path: setpoint above room temp → PID → t_set/CH-enable → gateway
  `HeatingActive` → heat call (validated once on the demo; redo on the
  rebuilt stack).
- Cooling path: Cool mode → cooling-enable bit → gateway
  `GetCoolingRequest()` → CoolingActive (requires gateway
  `smarthome.coolingSupportEnabled`).
- Remote override: reservation/smart-home setpoint push (ID 9) is adopted
  and echoed in ID 16 within the gateway's override timeout.
- DHW toggle + setpoint reach the boiler emulator.
- Sanity-check against a real OT boiler if one is available.
- Bench notes: gateway on LAN, drive/observe via KC1/TCP (see workspace
  serial-port notes); gateway dev branch = `ram`.

Done when: all of the above pass against an unmodified gateway build.
