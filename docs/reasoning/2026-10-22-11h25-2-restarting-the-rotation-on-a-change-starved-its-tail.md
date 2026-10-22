---
id: 2026-10-22-11h25-2
date: 2026-10-22
time: "11:25"
title: Restarting the OpenTherm rotation on every demand change starved its tail
builds-on:
supersedes:
---

**Before:** a changed demand restarted the OT rotation at slot 0, so the writes
went out within a couple of seconds. This looked free, because a demand change
was assumed to be rare.

**What changed it:** a panel rebooted in Cool showed the Cooling tile greyed
out. The tile dims until ID 3 (slave config) has been read, and ID 3 sat at the
end of the rotation. One slot goes out every 500 ms and the controller runs
every 5 s, so the rotation restarted every 10 slots whenever the demand
changed. In heating it changes at almost every step: the water setpoint moves
with the PI controller's integral. Every slot past the tenth was then read only
when the demand happened to stay unchanged for a whole pass. That covered the
slave config, the water setpoint bounds, the outside temperature and the
diagnostics. The demand itself is computed from two of those: cooling is gated
on ID 3, and the water setpoint is bounded by ID 49.

**Now:** priority in a cyclic schedule cannot be had by restarting it. Anything
behind the restart point inherits the change rate as its starvation condition.
Writes that must go out promptly are sent as a burst, and the rotation then
resumes where it was. Config that the demand depends on is read before
anything else whenever the link comes up.

Rendering showed the same blind spot. "Not read yet" had been drawn as "not
supported", so every boot greyed out a mode the guest had picked. Unknown and
absent are different states and need different defaults.
