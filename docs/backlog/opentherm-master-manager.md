# OpenTherm master manager

**Step 1, item 3.** `OpenThermManager`: the OT **master** loop (thermostat =
master, gateway = slave/boiler emulator) with typed accessors for everything
Step 1 needs.

- Poll loop: master status (ID 0, with CH-enable / **cooling-enable** / DHW-
  enable bits) ~1 s; write `t_set` (1), room setpoint (16), room temp (24),
  DHW setpoint (56); read slave status/config, and periodically: modulation
  (17), CH pressure (18), boiler temp (25), DHW temp (26), outside (27),
  return (28), max t_set bounds (57/49), OEM fault (5) + diagnostic (115).
- **Remote setpoint override (ID 9, flags ID 100)**: adopt gateway-pushed
  setpoints (reservations/smart-home), reflect in ID 16, allow local change
  afterwards. The gateway detects adoption by echo — a drop-in must do this.
- Link supervision: warm-up, timeouts, reconnect, link state exposed; safe
  state = no heat/cool demand when the link is down.
- Mirror the gateway's `OTHThermostatProps` field set so both ends agree.

Done when: gateway with THR=1 sees live room temp/setpoint and heat/cool
demand from this manager (same validation path as the old demo, plus ID 9).
