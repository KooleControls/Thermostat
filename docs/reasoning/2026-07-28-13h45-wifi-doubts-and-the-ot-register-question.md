---
id: 2026-07-28-13h45
date: 2026-07-28
time: "13:45"
title: WiFi link doubts — the real question is whether custom OT registers are allowed
supersedes:
---

Sticking with the WiFi link for now, but recording live doubts about it (Bas): the
gateway broadcasts an SSID anyone can try, typing the passphrase at every
installation is tedious, and while the thermostat is joined it is off the house
network so its own web UI is unreachable. Of those three, only the last is a genuine
structural argument for BLE — BLE is a separate radio, so it would not consume the
thermostat's single STA interface and the unit could stay on house WiFi permanently.
The first two do not actually improve with BLE: advertising is equally discoverable,
and the commissioning problem is *worse*, since three BLE pairing schemes were
already rejected on the thermostat being unable to tell which of several in-range
gateways is its own. The escape hatch named for BLE — publishing pairing data over
custom OpenTherm registers — resolves the WiFi version more cheaply (an SSID plus PSK
is ~40 bytes, against a BLE address plus a bonding secret and passkey dance), which
reframes the whole thing: **the decision is not BLE vs WiFi, it is whether custom OT
registers are acceptable given the open-source boundary.** If they are, both work and
WiFi is the cheaper build; if they are not, both still lack a commissioning story and
BLE's is the harder one. That puts RA2-396 back on the critical path after it briefly
looked irrelevant. Two middle paths stay open on the WiFi side: run the AP only when
there is work to do (removes the permanent broadcast, and mostly the reachability
complaint), or swap hosts so the *thermostat* runs APSTA and the gateway joins with
the STA that its Ethernet uplink leaves free (fixes reachability outright, but breaks
for a WiFi-uplink gateway). Cost asymmetry that argues against pivoting today: WiFi
needed essentially nothing thermostat-side and works end to end already, while BLE
needs the `SessionLink` extraction, a GATT service, bonding UI, and it reopens
WiFi/BLE coexistence on a classic ESP32 with a history of exactly that squeeze.

Decision: keep WiFi for now, do not pivot; treat "are custom OT registers allowed"
(RA2-396) as the question that actually decides this, and extract `SessionLink`
regardless so a later BLE move is a transport swap rather than a rewrite.
