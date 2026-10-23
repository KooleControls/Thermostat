---
id: 2026-10-23-11h41
date: 2026-10-23
time: "11:41"
title: Without a gateway on BLE the guest has no mode to pick, so the thermostat runs in Auto
builds-on: 2026-10-22-09h15, 2026-10-23-09h27
supersedes:
---

**Before:** the guest's mode was always in force. Over OpenTherm it showed
only in which requests went out (note 2026-10-22-09h15). The KC gateway read
the mode itself over BLE. With RA2-595 on hold (note 2026-10-23-09h27), BLE was
the only way the mode reached the gateway.

**What changed it:** we asked what a picked mode means when no BLE link carries
it. A gateway that has never paired, or has dropped off, cannot read the pick.
It then decides heating or cooling with its own logic. A guest who picked Heat
sends only heat requests to a gateway that may be cooling, and that gateway
cannot know why. The tiles offered a choice that nothing on the other side
could hear.

**Now:** the guest can pick a mode only while a gateway can hear it. Without
one, the thermostat runs the way the Honeywell it replaces does. It stays in
Auto and reports the room's needs in both directions, and the gateway picks
the side. The ID 0 status bits still show which side it picked. Off is not on
offer in that state, as it is not on the Honeywell. The guest's pick is
stored, and it is back in force when the gateway is.

This rests on the gateway handling Auto as it does for the Honeywell today. Two
cases differ. With no gateway paired, Auto applies at once. A paired gateway
that drops out gets two minutes to come back first, so that a reconnect does
not flip a Cool run to Auto and back.

**Follows:** `ClimateManager` gains a generic switch, `SetModeChoice`. It is
true by default, so a thermostat built without the BLE manager keeps every
mode. `BleManager`
clears the switch, since only the KC gateway needs the pick to travel. The
panel and the web UI show Auto alone while the choice is off. This uses the
Auto-only layout first built for RA2-595's bit 7. If that work resumes, bit 7
is a second reason to clear the same switch.
