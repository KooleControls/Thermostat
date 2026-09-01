---
id: 2026-09-01-08h55
date: 2026-09-01
time: "08:55"
title: Demand does not depend on the mode — only the setpoint does
builds-on:
supersedes:
---

**Before:** the mode was pinned to Auto in the UI, and the OpenTherm layer expressed
mode *through* demand: Heat pinned the heating request permanently on, and cooling
was only ever computed while the thermostat was in Cool.

**What changed it:** unlocking the panel's tiles exposed both halves as broken. A
permanently-on heat request would jam the gateway in `HeatingActive`, and computing
cooling only in Cool makes cooling unreachable — the gateway never sees the cooling
request it would need in order to change over. Underneath those two bugs sat the
actual realisation: demand answers "what does the room need", and that question does
not depend on the mode at all. Only the *setpoint* is mode-dependent, and only
because Off pins it to frost.

**Now:** the room's needs are reported in both directions in every mode, exactly as a
third-party thermostat already does, and the mode leaves the thermostat as intent for
the gateway to act on. The panel stores the guest's choice and deliberately does not
act on it: changeover timings, minimum on/off times, and whether cooling exists at
all are the gateway's knowledge, not ours — so acting locally could only ever
duplicate or contradict it. Rests on: the Cooling tile follows the gateway's
cooling-supported bit over OT, so an installation without cooling dims the tile and
refuses the touch — nothing KC-specific about that, a real boiler sets the same bit
with the same meaning.

Cross-reference: `esp_gateway/docs/reasoning/2026-09-01-08h55-intent-is-not-demand.md`,
which records why intent could not travel over OpenTherm at all.

**Follows:** four modes with Auto the default; the tiles are unlocked; and
`ClimateManager`'s demand calculation no longer branches on the mode.
