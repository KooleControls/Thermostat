# BLE link to the gateway — thermostat side

**Status: decided, not built.** Replaces the gateway-AP link
(`2026-07-27-gateway-ap-update-link.md`). Reasoning:
`docs/reasoning/2026-07-29-09h17-ble-instead-of-the-gateway-ap.md`.

The gateway is a BLE peripheral advertising **its DGID (`gatewayId`) as manufacturer data
plus its name in the scan-response packet** — two packets, because 31 bytes each will not
hold both. The **MAC needs no field**: it is in every packet header. The **install code is
never advertised**. The thermostat is the central: it scans and lists what it hears in a new
**BLE menu next to the WiFi menu**, showing **name as the primary row and DGID underneath**,
with a MAC suffix as the fallback when a unit is unprovisioned (`gatewayId` defaults to `0`,
`gatewayName` to `"NEW DOORLOCK GATEWAY"`). See
`docs/reasoning/2026-07-29-11h03-what-goes-in-the-advertisement.md`.
BLE then carries the **full `CommandManager` surface** — not a trigger, not a
firmware-only pipe — so `writePartition`, settings and everything else work over it
exactly as over the WebSocket.

The OpenTherm link stays the control/demand path. BLE is the management channel.

**Why (the only reason):** the thermostat stays reachable on **house WiFi in parallel**
with talking to the gateway — separate radio for the link, single STA left free for the
LAN, web UI never goes dark. Not a privacy or install-effort improvement: a BLE
advertisement is as visible as a broadcast SSID.
**But note who benefits:** thermostat WiFi is mainly a *development* aid and may be
**disabled in the field**, so BLE must stand alone — no falling back to WiFi for the big
transfer, and **field-relevant UI belongs on the display**, since the React UI is served
over HTTP and is unreachable over BLE. See
`docs/reasoning/2026-07-29-09h38-...md` and its correction `-10h55-3-wifi-is-a-development-convenience.md`.

**Radios:** the gateway does **Ethernet + WiFi STA + BLE and is never an AP**; the
thermostat does **STA when a network is available, its own AP when not** (the Strux
fallback, already built), **plus BLE always** for the gateway link. See
`docs/reasoning/2026-07-29-09h43-radio-split-between-the-two-boxes.md`.

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
3. **Pairing: the gateway's 6 public install-code digits as the BLE passkey.** The
   gateway holds it fixed (`system.installCode`, ASCII, default `"000000"`); the installer
   types it on the **touchscreen** — display-first, not browser-only. Then bond, store the
   peer in settings, auto-reconnect and never stop retrying (see
   `docs/reasoning/2026-07-27-16h17-never-stop-retrying-the-link.md`). Accepted: one code
   per resort, and a fixed passkey is sniffable in principle — this keeps the curious out,
   it is not a foolproof channel. See `-10h55-2-picked-by-name-gated-by-the-install-code.md`.
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
- ~~Merge `feature/wifi-diagnostics`~~ — done, merged into `main` 2026-07-29.
- **NTP through the gateway** is no longer free: with no shared IP link the thermostat
  needs its own route to an NTP server, or the time has to come over BLE as a command.
- The **`www` partition** is a second transfer over the same mechanism, and local `.bin`
  upload via `FirmwarePage` stays the offline floor.

## Open-source boundary

KC-specific code **is allowed here as long as it stays inside the BLE manager** — a third
party can delete that module, plug in their own transport, and everything else (display,
control loop, OpenTherm, settings, commands, web UI) still serves them. The prohibition is
on leaking *existing* KC internals (connection-server protocol, keys, fleet semantics), not
on KC-ness. So the GATT/pairing contract can be plainly KC's. See
`docs/reasoning/2026-07-29-10h55-kc-specific-code-lives-in-the-ble-manager.md`.

## Relations

- Tracked under **RA2-437**; pairing no longer depends on **RA2-396** (custom OT
  registers) — that question is off the critical path for this.
- Gateway side: `esp_gateway/docs/reasoning/2026-07-29-09h17-gateway-advertises-over-ble.md`.
  The WebSocket-client half of
  `esp_gateway/docs/plans/2026-07-27-thermostat-firmware-push.md` is void; the
  `CFUP <target> <url> <sha256> <size>` shape and hash-verified fetch survive.
