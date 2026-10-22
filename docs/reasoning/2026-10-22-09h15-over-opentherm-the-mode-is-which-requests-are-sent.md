---
id: 2026-10-22-09h15
date: 2026-10-22
time: "09:15"
title: Over OpenTherm the mode is which requests are sent
builds-on:
supersedes: 2026-09-01-08h55
---

**Before:** demand answered "what does the room need" in both directions, in
every mode. The panel mode travelled to the gateway separately, over BLE, and
the gateway picked which direction to run. We believed this was how a
third-party thermostat behaves (note 2026-09-01-08h55).

**What changed it:** three things, found together.

1. The Honeywell this thermostat replaces does not behave that way. In Cool it
   sends cooling and nothing else.
2. The gateway log, with the panel in Cool, showed a heat request whenever the
   setpoint went above the room. After the setpoint came back down, that
   request stayed on beside the cooling request for 50–75 s: the PI
   controller's averaging window still held the earlier full-output samples.
   Off had the mirror-image fault. Pinning the setpoint to 5 °C frost put
   nearly any room above the cooling threshold, so Off requested cooling all
   the time.
3. Only the KC gateway reads the mode over BLE. A plain OpenTherm boiler never
   learns which tile was pressed, so a heat request sent in Cool fires it. That
   fails the test that the thermostat still works with `BleManager` removed.

**Now:** on the OpenTherm wire, the mode is which requests are sent, and that
is the only way to say it there. Heat sends heating, Cool sends cooling, and Off
sends heating against the frost setpoint. Auto still reports both directions,
because the changeover is the gateway's decision. This is the part of the old
note that still holds: cooling has to be reachable or the gateway can never
change over. But Auto never sends both at once, because a room cannot need heat
and cooling together. For the KC gateway, reading the mode over BLE is extra
information on top of this, not the thing that keeps the boiler honest.
Changeover delays and minimum on/off times stay with the gateway, as before.

This rests on the Honeywell's behaviour as described from the field. There is
no wire capture of it yet. Still open: the Honeywell also sends cooling as a
percentage (OT ID 7), which we do not send at all.

**Follows:** `ClimateManager::ControlStep` gates each direction by mode and
resets the PI controller whenever it is not heating, so no stale heat request
can carry over into cooling.
