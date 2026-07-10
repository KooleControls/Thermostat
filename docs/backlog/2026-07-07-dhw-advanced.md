# DHW advanced features (consider before shipping)

**Status: deferred, out of scope for `hot-water` (item 6).** These came up
while designing basic DHW (2026-07-07). Basic DHW (enable + setpoint +
readout) intentionally ships first; these are the "before we call the
product done" list — not blockers for the baseline thermostat.

## Legionella protection (highest priority)

Periodic high-temperature disinfection cycle (raise DHW to ≥ 60 °C, commonly
periodically e.g. weekly, for a sustained period) to kill *Legionella* in the
tank. This is a **safety/compliance** consideration for a shipping DHW
product, not a comfort feature. Needs its own brainstorm: schedule, target
temp/duration, how it interacts with the user setpoint, and whether the
boiler already does this itself (many do — check the OT slave capabilities
before we build our own).

## DHW scheduling / comfort profiles

Time-based DHW enable or setpoint programs (e.g. hot water only in the
morning/evening, eco vs comfort). Depends on the broader scheduling story
(there is no reservation/schedule engine on the Strux baseline yet — shared
with the room-climate scheduling question).

## Notes

- Basic DHW (item 6) is a plain enable + setpoint passthrough; the boiler
  owns tank heating. It has no control loop, so these features would layer
  a scheduler/state-machine on top of `HotWaterManager` when built.
- Revisit before a production release; none block the Step-1 "normal
  thermostat" milestone.
