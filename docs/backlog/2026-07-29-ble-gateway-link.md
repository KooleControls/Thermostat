# BLE link to the gateway — thermostat side

**Status: decided, not built.** Replaces the gateway-AP link
(`2026-07-27-gateway-ap-update-link.md`). Reasoning:
`docs/reasoning/2026-07-29-09h17-ble-instead-of-the-gateway-ap.md`.

The gateway is a BLE peripheral advertising **its device name + gateway ID**
(`DGWN`/`DGID`). The thermostat is the central: it scans, lists what it finds in a new
**BLE menu next to the WiFi menu**, and the installer picks the right gateway by name.
BLE then carries the **full `CommandManager` surface** — not a trigger, not a
firmware-only pipe — so `writePartition`, settings and everything else work over it
exactly as over the WebSocket.

The OpenTherm link stays the control/demand path. BLE is the management channel.

**Why (the only reason):** the thermostat stays reachable on **house WiFi in parallel**
with talking to the gateway — separate radio for the link, single STA left free for the
LAN, web UI never goes dark. Not a privacy or install-effort improvement: a BLE
advertisement is as visible as a broadcast SSID. See
`docs/reasoning/2026-07-29-09h38-the-ble-pivot-is-about-parallel-availability.md`.

## Steps

1. **Extract `SessionLink` from `Session`.** `Session` holds a concrete
   `WsSessionLink&`; BLE needs the transport behind an interface so `SessionMux` /
   `AuthGate` / `ConnectionRegistry` are transport-agnostic. This is the load-bearing
   refactor — it was already worth doing on its own (see the 2026-07-28 note) and every
   other step sits on it.
2. **GATT service carrying the session-mux framing.** Same
   `[session:u16 LE][flags:u8][payload]` frames as `/ws`, chunked to the negotiated
   MTU. Notify (gateway→thermostat) + write (thermostat→gateway), or the mirror of it —
   decide once the roles are wired. The existing `BleManager` +
   `KCThermoBleProtocol.h` demo code is the precedent for the transport, but its three
   fixed characteristics are not the protocol we want.
3. **Pairing / bonding UI.** Scan list in the service menu, connect, bond, store the
   gateway address in settings. Then auto-reconnect and never stop retrying (see
   `docs/reasoning/2026-07-27-16h17-never-stop-retrying-the-link.md`).
4. **Auth over BLE.** In-band `hello` → `login`/`auth` is already per-connection, so it
   carries over — but `web.password` defaults to empty (auth off), so decide whether a
   bonded BLE peer counts as authenticated or the gateway has to hold a credential.
5. **Throughput check.** ~1–2 min for a 1.6 MB image at 1M PHY is the accepted budget;
   measure it before promising unattended updates, and make sure progress reporting
   survives a link that slow.

## Carried over from the AP doc (still true, not BLE-specific)

- **Fix lost-IP recovery.** `Ipv4Lost` clears `staConnected_` and nothing retries; a
  lease lost while still associated strands the device until reboot. Now purely about
  house WiFi, but still a real bug.
- **Merge `feature/wifi-diagnostics`** — disconnect reason code + passphrase length.
- **NTP through the gateway** is no longer free: with no shared IP link the thermostat
  needs its own route to an NTP server, or the time has to come over BLE as a command.
- The **`www` partition** is a second transfer over the same mechanism, and local `.bin`
  upload via `FirmwarePage` stays the offline floor.

## Relations

- Tracked under **RA2-437**; pairing no longer depends on **RA2-396** (custom OT
  registers) — that question is off the critical path for this.
- Gateway side: `esp_gateway/docs/reasoning/2026-07-29-09h17-gateway-advertises-over-ble.md`.
  The WebSocket-client half of
  `esp_gateway/docs/plans/2026-07-27-thermostat-firmware-push.md` is void; the
  `CFUP <target> <url> <sha256> <size>` shape and hash-verified fetch survive.
