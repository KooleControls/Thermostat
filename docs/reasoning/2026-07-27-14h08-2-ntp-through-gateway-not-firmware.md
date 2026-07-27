---
id: 2026-07-27-14h08-2
date: 2026-07-27
time: "14:08"
title: Route NTP through the gateway, but not firmware
supersedes:
---

The gateway will NAT the thermostat's traffic to the internet
(`esp_netif_napt_enable()`), but the intended scope is **NTP only** — plain UDP, no
certificates — so the thermostat gets a correct clock without depending on the
customer's WiFi being reachable. That matters because certificate validation needs
a sane clock, and a unit that boots with no time source fails TLS in a way that
looks like nothing. Rejected the tempting follow-on of letting the thermostat fetch
its own firmware again now that it has internet: that reinstates every reason the
GitHub-pull direction was dropped (a CA bundle that expires in the field, TLS heap,
dependence on a host's undocumented redirect behaviour) and it is exactly the
conflation worth avoiding — "has internet" and "is trusted to fetch its own
firmware" are separate decisions. Firmware keeps coming from the gateway over the
local link (see [[2026-07-27-14h08]]). Nothing here is load-bearing for security;
NTP is unauthenticated either way.

Decision: NAT the gateway's uplink for NTP only; firmware still comes from the
gateway, not the internet.
