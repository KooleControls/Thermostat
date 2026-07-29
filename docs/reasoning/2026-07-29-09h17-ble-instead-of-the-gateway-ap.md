---
id: 2026-07-29-09h17
date: 2026-07-29
time: "09:17"
title: BLE instead of the gateway AP, with commands routed through CommandManager
supersedes: 2026-07-27-14h08, 2026-07-28-13h45
---

Pivoting the thermostat↔gateway *management* link from the gateway's WiFi AP to BLE
(the OpenTherm link stays the control/demand path — this only replaces how firmware,
config and commands reach the unit). What unblocks it is commissioning, which is the
ground both earlier notes rejected BLE on: the gateway advertises under its **device
name plus its gateway ID** (`DGWN`/`DGID`, the same per-unit id the AP SSID derived
from), and the **thermostat** gets a BLE menu next to the existing WiFi menu that
lists what it scans. So the human picks the right gateway off a screen that already
exists in the product — the 2026-07-27 rejection assumed the gateway would have to
choose a thermostat (it has no UI) or that a device in pairing mode couldn't tell
which of several in-range gateways was its own, and neither survives putting the
chooser on the thermostat with an identity in the advertisement. That also drops
custom OT registers (RA2-396) off the critical path for pairing, which the 2026-07-28
note had made the deciding question. The payoff is the one genuinely structural
argument from that note: BLE is a separate radio, so the thermostat's single STA stays
on house WiFi and its own web UI remains reachable — no APSTA-on-the-thermostat
question, no permanent gateway SSID for anyone to try, no passphrase typed at every
install. BLE is a **full transport routed into `CommandManager`, not a trigger
channel** — every command works over it, which forces the `SessionLink` extraction out
of `Session`'s concrete `WsSessionLink&` that the 2026-07-28 note already said to do
regardless. Costs accepted: ~1–2 minutes for a 1.6 MB image at 1M PHY against seconds
over WiFi TCP, a GATT service plus bonding/passkey work, and WiFi/BLE coexistence on
the gateway (which the DRAM reclaim showed does work). The 2026-07-27 rejection of the
GitHub pull is untouched and still stands. Downstream: the gateway-AP work in
`docs/backlog/2026-07-27-gateway-ap-update-link.md` and the gateway's
`thermostat-firmware-push` plan lose their transport, and the thermostat's KC-BLE demo
link (`BleManager`) becomes the thing to mine rather than dead code.

Decision: BLE is the gateway↔thermostat management link; gateway advertises name +
gateway ID, thermostat scans and picks it in a new BLE menu, and BLE carries the full
`CommandManager` surface.
