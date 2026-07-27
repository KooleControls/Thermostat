---
id: 2026-07-27-14h08
date: 2026-07-27
time: "14:08"
title: Gateway AP + existing WebSocket, not BLE, for firmware push
supersedes:
---

Firmware reaches the thermostat over a WiFi AP the gateway runs (APSTA — house
WiFi on its STA, AP for the thermostat) driven by the WebSocket command surface
that already exists, rather than over BLE. The deciding factor was that the
thermostat side is already built: WS transport, `AuthGate` and streamed
`writePartition` all ship, and the gateway's AP is just another SSID in the WiFi
screen — where BLE needs a `SessionLink` extraction out of `Session`'s concrete
`WsSessionLink&`, a GATT service, bonding and a passkey UI. Throughput agrees:
~1–3 MB/s of WiFi TCP puts a 1.6 MB image in seconds against 1–2 minutes over BLE
capped by the gateway's 1M PHY. Rejected BLE in three forms — gateway-advertises
(the gateway has no UI to choose a thermostat), thermostat-advertises-with-passkey
(multiple gateways in RF range all grab a device in pairing mode), and using
OpenTherm to identify the right gateway (ID 3 carries a *manufacturer* MemberID,
identical across KC gateways, so the wire cannot disambiguate two of them, and
anything richer means custom data-IDs, which is wrong for an open-source product).
Also rejected the earlier GitHub-pull direction it supersedes: a compiled-in CA
bundle expiring in a unit that sits in a hallway for years, ~40 KB of TLS heap, and
undocumented GitHub behaviour we already tripped over twice (a 302 to a storage
host and a pre-signed query string that overran the default request buffer). The
cost accepted is that the thermostat's single STA is then on the gateway's AP, so
it is off the house network unless it runs APSTA too, and one radio means the AP
follows the STA's channel.

Decision: gateway runs an AP and pushes firmware over the existing WebSocket
command surface; BLE and the GitHub pull are both dropped.
