---
id: 2026-07-27-16h17
date: 2026-07-27
time: "16:17"
title: The thermostat must never stop retrying its WiFi link
supersedes:
---

The thermostat retries its configured network forever, with no terminal state. It
currently does not: Strux tries three times, falls back to hosting its own AP, and
then never attempts the configured SSID again — which we hit for real today, because
after the gateway rebooted the thermostat had exhausted its attempts and only came
back after a manual reset. That is acceptable for a bench unit and unacceptable for
the product: the device is on a wall in someone's house, and the WiFi link is now the
*only* remote path to it, so a unit that gives up is unreachable forever and needs a
site visit — the exact cost this whole update mechanism exists to avoid. Every
ordinary event causes it: the gateway reboots for its own firmware update, the router
restarts, power blips, someone is out of range for an hour. Rejected keeping the
three-attempt cap with a longer timeout (it still has a terminal state, just a later
one) and rejected dropping the AP fallback (it is the only way in when no credentials
are configured at all, and it must stay). The shape is therefore: keep the AP
fallback as a *concurrent* or *interleaved* state, not a final one, and retry the
configured SSID indefinitely with a backoff that settles at some modest interval
rather than hammering the radio. Related: `Ipv4Lost` currently clears `staConnected_`
without triggering anything, so a lease lost while still associated is another way to
end up stranded — same class of bug, same fix in spirit.

Decision: retry the configured WiFi indefinitely with backoff; no state the
thermostat can reach where it stops trying.
