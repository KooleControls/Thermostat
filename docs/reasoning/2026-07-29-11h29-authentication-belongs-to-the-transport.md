---
id: 2026-07-29-11h29
date: 2026-07-29
time: "11:29"
title: Authentication belongs to the transport, not to the session layer
supersedes:
---

Bas, on whether the auth layer is shared between WebSocket and BLE: it should not be —
"the ble pin is already the auth, I dont think we need an extra login for this phy."
Correct, and it lands the open question from an hour ago. **Each transport authenticates
its own peer by whatever means suits that physical link**, and the session layer above
receives only traffic the transport has already vouched for. A browser over WiFi needs a
password plus a bearer token, because anyone who can reach the socket can open one — that
is `Authenticator`, `SessionTable`, `AuthGate` and the in-band `hello`/`login`/`auth`
verbs, and they are hereby **WebSocket-specific rather than shared**. BLE proves the peer
at the link layer instead: pairing with the install-code passkey plus bonding, so a JSON
login on top would be theatre — it would re-ask a question the radio already answered, and
worse, it would imply a second credential that nothing provisions. So `BleManager` gates on
"is this connection encrypted and bonded" and dispatches sessions only then; `web.password`
stays a WiFi concern and does not gate BLE at all. `AuthGate` goes back to taking a concrete
`WsSessionLink&` rather than the transport-agnostic `SessionLink&` I had just given it —
the narrower type is the honest one and stops a future reader assuming auth is a shared
layer. Two consequences: the planned `WsConnection` split (generalising the per-connection
state so a BLE peer could share `AuthGate`) is **cancelled — not needed at all**, since
nothing BLE touches that struct; and the BLE side must genuinely *verify* the link state
from NimBLE rather than assume it, because "bonded" is what replaces `conn.authed` and an
unencrypted connection must dispatch nothing.

Decision: authentication is per-transport. WebSocket keeps password + token; BLE relies on
pairing + bonding with no login handshake; `AuthGate` and friends are WebSocket-specific and
the `WsConnection` generalisation is dropped.
