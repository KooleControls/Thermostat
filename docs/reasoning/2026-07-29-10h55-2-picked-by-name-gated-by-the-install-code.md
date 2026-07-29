---
id: 2026-07-29-10h55-2
date: 2026-07-29
time: "10:55"
title: Picked by name, gated by the gateway's install code
supersedes: 2026-07-29-09h17
---

The 09h17 note said the gateway would advertise "its device name plus its gateway ID
(DGWN/DGID)", and reading the gateway's `SystemSettings` shows that cannot be relied on:
`gatewayId` is a `uint32` defaulting to **0**, and `gatewayName[40]` defaults to **"NEW
DOORLOCK GATEWAY"** — identical on every unit until an installer sets it. Bas put it more
bluntly: the gateway doesn't have an id. So the advertisement carries the name *plus a
suffix derived from the gateway's BLE MAC*, which is unique by construction and needs no
provisioning, and the name is a nicety rather than the identifier. Security comes from a
separate thing: **the gateway's 6 public install-code digits** (`system.installCode`, ASCII,
default `"000000"`), used as BLE's own six-digit passkey — the gateway holds it fixed, the
installer types it on the thermostat's touchscreen, and bonding follows. That is a
deliberate fit to what already exists: the code is generated and held on the gateway side,
so nothing about KC internals crosses into this repo, and the 4 private digits stay out of
it entirely. Bas accepts the weakness openly — one code covers a whole resort, so any
thermostat there can pair with any gateway, and a fixed passkey is in principle
recoverable by someone sniffing the pairing: "I dont think we should treat this as a fool
proof connection but as a keep the curious ones outside." Rejected: a per-unit secret (none
exists, and inventing one means provisioning hardware that currently ships with defaults),
just-works pairing (what the old demo did — anything in range pairs silently), and using
the private digits.

Decision: advertise gateway name + BLE-MAC suffix for identification; gate pairing with the
6 public install-code digits as the BLE passkey, entered on the thermostat's screen.
