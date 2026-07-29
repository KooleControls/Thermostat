---
id: 2026-07-29-11h03
date: 2026-07-29
time: "11:03"
title: What goes in the advertisement — name and DGID, never the install code
supersedes: 2026-07-29-10h55-2
---

Replacing only the *identification* half of 10h55-2 — the install-code-as-BLE-passkey
decision there stands untouched. An advertisement is a list of typed fields, not one string,
so we can carry both: **`gatewayId` (DGID) as manufacturer data in the advertising packet,
and the gateway name in the scan-response packet** — the second packet a scanner asks for
after hearing the first. The split is forced by size, which is the concrete reason to write
this down: each packet holds 31 bytes of fields, the mandatory flags field eats 3, a 4-byte
DGID in manufacturer data costs 8 with its headers, and a 20-character name costs 22 — so
name plus DGID does not fit in one packet, and `gatewayName[40]` has to be truncated to ~29
characters even alone in the scan response. **The MAC address needs no field at all**: it
rides in every packet's header, so the thermostat already knows it — which retires the
"advertise a MAC-derived suffix" idea from 10h55-2 as a thing we send, though a suffix of
what we *receive* is still the right tiebreaker for display when DGID is 0 or two units
collide. One caveat that comes with relying on it: the address is only stable if the gateway
keeps a public/static address and we do not turn on BLE privacy (resolvable private
addresses rotate by design). **The install code is deliberately not in the advertisement**
(Bas: "ist should be hidden") — and as a passkey it is never transmitted at all, it only
feeds the pairing maths. For the scan list: **name is the primary row, DGID the second**,
with the received-MAC suffix as the fallback line when there is nothing better. Open item:
manufacturer data is keyed by a Bluetooth-SIG company id and KC does not appear to have one,
so this starts on the `0xFFFF` test id unless someone registers one.

Decision: advertise DGID in manufacturer data plus the (truncated) name in the scan response;
never advertise the install code; take the MAC from the packet header and show name-primary,
DGID-secondary in the scan list.
