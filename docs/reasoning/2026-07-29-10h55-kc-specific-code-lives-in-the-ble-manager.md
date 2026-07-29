---
id: 2026-07-29-10h55
date: 2026-07-29
time: "10:55"
title: KC-specific code is allowed, as long as it stays inside the BLE manager
supersedes:
---

Refining the open-source boundary (Bas): the rule is **not** "nothing KC-flavoured in this
repo." It is (1) **don't leak existing KC internals** — the connection-server protocol,
keys, fleet semantics — and (2) **new KC-specific code is fine if it is confined to the
BLE manager**, because a third party can delete that one module and drop in WiFi or a
transport of their own while the display, control loop, OpenTherm, settings, commands and
web UI all remain useful to them. That gives a concrete test to apply when in doubt: could
someone rip out `BleManager` and still have a working generic thermostat? If yes, the KC
bit is acceptable. This retires the stricter reading I had been applying an hour earlier —
that the BLE contract had to be vendor-neutral, with "KC" kept out of service names and
UUIDs — which would have cost us an invented neutral protocol for no benefit to anyone.
So the GATT/pairing contract can be plainly KC's, and can live in this public repo as
such. What does *not* change: nothing from the KC2/connection-server side comes across,
and the generic `CommandManager` surface stays the thing BLE plugs into rather than
growing KC semantics of its own.

Decision: KC-specific code is permitted in this repo when it is isolated in the BLE
manager and rip-out-able; the prohibition is on leaking existing KC internals, not on
KC-ness.
