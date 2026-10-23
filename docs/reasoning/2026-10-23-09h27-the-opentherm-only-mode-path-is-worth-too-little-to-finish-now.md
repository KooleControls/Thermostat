---
id: 2026-10-23-09h27
date: 2026-10-23
time: "09:27"
title: The OpenTherm-only mode path reaches too few sites to finish now, so the mode stays on BLE
builds-on: 2026-10-22-11h25
supersedes:
---

**Before:** honouring OT ID 3 bit 7 looked like the way to carry the mode, so
that the panel's choice would need no BLE. That would make the thermostat fit
OpenTherm-only sites where a Honeywell is installed today (note 2026-10-22-11h25).

**What changed it:** we weighed what finishing it would cost against what it
would reach. The remaining work is more testing, the issues that testing
turns up, and the gateway side of RA2-595. The OpenTherm-only path would serve
only about 90 installations. The mode over BLE already works. The next thermostat will have an RS485 connection, so the
OpenTherm-only path would only ever serve this hardware generation.

**Now:** the technical understanding of note 2026-10-22-11h25 still holds. The
plant grants the heat/cool choice over OpenTherm, and bit 7 is how. What has
changed is the value of acting on it. With limited time, a path that reaches
about 90 sites and has a successor already planned is not worth finishing now.
The mode stays the guest's pick, and the gateway reads it over BLE. It can be
picked up again once it is tested further and the remaining issues are fixed.

**Follows:** RA2-595 is on hold. On the thermostat its code lives on
`feature/RA2-595-heat-cool-control` as one commit on top of the branch that
carries everything else.
