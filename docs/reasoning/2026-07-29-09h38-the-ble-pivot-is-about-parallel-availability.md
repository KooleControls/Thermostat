---
id: 2026-07-29-09h38
date: 2026-07-29
time: "09:38"
title: The BLE pivot buys parallel availability, and nothing else
supersedes: 2026-07-29-09h17
---

The 09h17 note listed three payoffs for the BLE pivot; Bas corrects that only one is
the goal. **The requirement is that the thermostat stays reachable over house WiFi
while it is communicating with the gateway** — one radio for the gateway link, the
single STA left free for the LAN, so its own web UI never goes dark. The other two were
mine, not the brief, and are explicitly **out of scope**: "anyone can see the broadcast
SSID" is not a problem we are solving (BLE advertising is just as discoverable, so BLE
does not fix it), and the passphrase typed at each install is not the driver either.
Picking the gateway by name off a scan list is the **mechanism** that dissolves the old
commissioning objection — it is what makes BLE possible, not a benefit being bought.
Consequences for how this gets argued later: do not defend the pivot on privacy or
install effort, because it does not deliver either; and any alternative that restores
parallel availability another way (thermostat runs APSTA, or the gateway raises its AP
only while work is pending) competes on the real requirement rather than being dismissed
out of hand. Everything else in 09h17 stands — BLE as a full `CommandManager` transport,
the `SessionLink` extraction, OpenTherm keeping the control path, the accepted ~1–2 min
per image.

Decision: the BLE pivot is justified solely by keeping the thermostat on house WiFi in
parallel with the gateway link; broadcast visibility and install effort are not goals.
