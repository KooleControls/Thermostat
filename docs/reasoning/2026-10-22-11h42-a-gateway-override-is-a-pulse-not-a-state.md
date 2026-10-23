---
id: 2026-10-22-11h42
date: 2026-10-22
time: "11:42"
title: A gateway setpoint override arrives as a pulse, not a state
builds-on:
supersedes:
---

**Before:** we read OT ID 9 (remote override room setpoint) as a level. While it
was non-zero the override was in force, and the control step adopted it whenever
it saw it.

**What changed it:** reading the KC OpenTherm module's firmware
(`ThermostatOpenthermThread.c`, the module between gateway and thermostat). A
gateway override (`OVE`) and the hold on a heat/cool switch go out the same way:
the value for three ID 9 reads, then 0 for three more, then 0 for good. We read
ID 9 once a second, so the value is visible for about 3 s. The control step
looks every 5 s, so it caught the pulse about three times in five.

The gateway of the Honeywell era (V00.17.06) never re-sends an override that was
missed. Its max-setpoint clamp waits for the setpoint to change, and the setpoint
never changes if the override never landed. Arrival, night, vacant, the
setpoint restore after boot and the clamp all travel this way. On those sites a
missed pulse is a missed setback.

**Now:** an override is an event, so it has to be latched where it is read and
adopted once. Reading it as a level was never safe against this slave. It looked
safe on the current gateway only because that gateway retries after a timeout.
Adopting once also leaves the guest free to change the setpoint afterwards.
Re-adopting a level that stayed set would pin the setpoint for as long as a
slave held it. Rests on: the module's r45 behaviour as read from its source.
