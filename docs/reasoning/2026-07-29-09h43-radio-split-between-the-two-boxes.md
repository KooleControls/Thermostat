---
id: 2026-07-29-09h43
date: 2026-07-29
time: "09:43"
title: Radio split — gateway does Ethernet + STA + BLE, thermostat does AP-or-STA + BLE
supersedes:
---

Nailing down what each box's radios do, because 09h17 only said the gateway stops
hosting an AP *for the thermostat* and Bas is clear it is stronger than that. **The
gateway does Ethernet, WiFi STA and BLE — it is not an access point at all**, so APSTA
is off the table rather than merely unused for this link. **The thermostat does WiFi
STA when a network is available and its own AP when it is not** — the Strux fallback
already built and reachable from the WiFi screen — **plus BLE, always, for the gateway
link.** That satisfies the 09h38 requirement on both legs: with a house network the
thermostat sits on the LAN and its web UI is reachable there, without one it serves that
same UI off its own AP, and in neither case does the gateway link touch the STA. Rejected
keeping a gateway AP as a fallback transport, which is what the 2026-07-27 direction was
built on: it would mean the gateway either burns its STA or juggles APSTA to hand out an
SSID that now carries nothing, and BLE already covers the case where the thermostat has
no network at all — a unit with no WiFi and no BLE peer has no path either way. The
consequence worth flagging: NTP-through-the-gateway (NAPT over the shared AP, note
`2026-07-27-14h08-2`) dies with the AP — there is no shared IP link between the boxes any
more, so the clock comes from the thermostat's own STA when it has one, or over BLE as a
command, or not at all in AP mode.

Decision: gateway = Ethernet + WiFi STA + BLE, never an AP; thermostat = WiFi STA with AP
fallback, plus BLE for the gateway link.
