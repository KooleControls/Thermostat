---
id: 2026-07-29-10h55-3
date: 2026-07-29
time: "10:55"
title: WiFi is a development convenience; BLE has to stand alone
supersedes: 2026-07-29-09h38
---

Bas: the thermostat's WiFi is mainly for his own development, and he would not be surprised
if it ends up **disabled in the field** — to be discussed with a colleague, so keep it
working for now. That reframes what 09h38 recorded. Parallel availability is still the
reason BLE beats the gateway's AP, and privacy and install effort are still *not* goals, but
the party who benefits from parallel availability is largely **us during development**: a
field unit may have no WiFi at all. The consequences are what make this worth its own note.
**BLE has to be self-sufficient** — firmware, config, diagnostics, everything — with no
"fall back to WiFi for the big transfer" escape hatch, which makes the ~1–2 minute image
transfer the real path rather than a degraded one, and makes the reliability of a slow link
(progress, timeouts, resuming after a drop) a first-class concern instead of a nicety. And
**the React web UI becomes a development tool**: it is served over HTTP and cannot be
reached over BLE, so on a WiFi-less unit the only interfaces are the touchscreen and
whatever the gateway chooses to expose. Anything built *only* in the browser is invisible in
the field — the pairing UI in particular has to exist on the display, not just as a web page.
Rejected the reading that WiFi-may-be-off weakens the case for BLE: it strengthens it, since
BLE stops being the alternative link and becomes the only one.

Decision: treat thermostat WiFi as a development aid that may be switched off in the field;
BLE must carry everything on its own, and field-relevant UI belongs on the display.
