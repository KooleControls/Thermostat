---
id: 2026-10-22-11h25
date: 2026-10-22
time: "11:25"
title: The plant grants the heat/cool choice over OpenTherm, so the mode needs no BLE
builds-on: 2026-10-22-09h15
supersedes:
---

**Before:** the guest's mode was always in force on the thermostat. The only way
the gateway learned it was by reading it over BLE. Behind that sat an untested
assumption: an OpenTherm-only installation could never let the guest choose
heating or cooling. The Honeywell installations seemed to prove it, because they
are stuck in Auto.

**What changed it:** RA2-550 showed on the OT testbench that the Honeywell is
stuck in Auto because of one bit, and that the bit is standard OpenTherm. ID 3
high byte, bit 7 (heat/cool mode control) is where the slave says who switches
between heating and cooling. The gateway always sent 1, meaning "the slave
does". With it cleared, the Honeywell's mode buttons work, and its requests
alone tell the gateway which side to run. RA2-595 moves the gateway to that.

**Now:** whether the thermostat may choose heating or cooling is not ours to
assume. The plant grants it over OpenTherm.
- **Bit 7 set:** Auto is the only mode. The room's needs go out in both
  directions, and the plant picks the side.
- **Bit 7 clear:** the guest's mode is in force. It reaches the plant purely
  through which requests are sent (note 2026-10-22-09h15).

Neither case needs BLE. That makes the thermostat a candidate for OT-only sites
where a Honeywell is installed today, once the gateway runs RA2-595.

This rests on two things: the gateway clearing the bit, and the gateway
following the requests instead of deciding the side from room temperature
against setpoint. Both are RA2-595 work on the gateway side.

Still open: whether the gateway keeps reading the mode over BLE at all, and
whether its changeover delays make the response acceptable when they only start
counting once a request arrives.

**Follows:** the thermostat reads bit 7. While it is set, the thermostat runs in
Auto and the panel and web UI offer only Auto. The guest's stored pick is kept
for when the choice comes back.
