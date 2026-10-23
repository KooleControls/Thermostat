---
id: 2026-10-22-13h28
date: 2026-10-22
time: "13:28"
title: A dialled setpoint goes out once, because each change steers the gateway
builds-on: 2026-10-22-11h25
supersedes:
---

**Before:** each press of -/+ changed the setpoint straight away, and the next
control step sent it over OpenTherm. The setpoint was visible only by pressing
-/+, so checking it changed it. We treated that as a cosmetic quirk.

**What changed it:** two things. First, a field test on a V00.17.06 gateway made
plain that the guest cannot look without touching. Second, that gateway's
`HeatManager` turned out to read the setpoint's direction, not only its value.
A drop makes it want cooling (if cooling is allowed and the room is warm
enough), and a rise makes it want heating. Dialling from 22 to 19 and back to 21
one degree at a time sent every step between. To that gateway that was three
turn-downs and two turn-ups, each steering it toward the other side.

**Now:** a setpoint change is an event to the gateway, not just a value. So the
panel has to send one change per decision, not one per press. Reading the
setpoint is separate from setting it:
- The setpoint is always on the face.
- The first touch only shows it.
- What is dialled is handed over once, after the buttons go quiet.

This also makes a remote override visible as it lands, which a hidden setpoint
never could.
