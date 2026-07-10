# Unified WebSocket transport (Strux redesign)

**Status: brainstorm, not committed (2026-07-09).** This is a Strux-template architecture
direction, explored while debugging a Console timeout in this project. It should
land as a Strux issue/spec and be developed in its own cycle. Nothing here is
built yet.

## Where this came from

Debugging item 8, the Console page timed out. Root cause: `getLogs` builds its
reply into `WebSocketHandler`'s fixed **4096-byte `wsBuf_`**; a full log ring
(`ConsoleManager` `MAX_LINES=200 × MAX_LINE_LEN=200`, ~40 KB) overflows it, the
frame is truncated to invalid JSON, the browser drops it, and `send("getLogs")`
hits its 10 s timeout. Alongside it we saw recurring `httpd_sock_err: recv 104`
(ECONNRESET) — the single-threaded httpd (socket pool ~7) with `lru_purge_enable`
evicting the quietest socket (often our own WebSocket) under load. Both point at
the same structural issue: **too many sockets, and a WS reply path that doesn't
stream.**

## The idea

Do everything over **one WebSocket per client**, except the static assets needed
to bootstrap the page. For remote access, the device **dials out** to a server so
there's no port-forwarding; the server bridges browser ↔ device by forwarding
that one socket.

**Motivations**
- **Socket economy.** One long-lived socket per client instead of N (WS + a
  `fetch` per `/api/command` + static asset requests). Kills the
  LRU-purge/ECONNRESET class of bug on the tiny embedded server.
- **Trivial remote bridging.** A single tunnel per device means the relay
  forwards one connection and understands nothing — no per-endpoint routing.
- **One code path.** Every device interaction is a `CommandManager` command over
  one transport — uses the Strux framework as intended.

## Architecture (decided so far)

- **HTTP: static bootstrap only.** The app (index/JS/CSS) still loads over HTTP —
  you can't open a WS without a loaded page. Remotely, the relay/cloud can serve
  the static app while only the WS tunnels to the device. Split: *bytes that
  describe the app → HTTP; bytes that talk to the device → the one WS.*
- **One WS per client** carries login, all commands, and large/binary transfers.
- **Chunked + interleaved (multiplexed).** Large replies and binary transfers
  (firmware upload, partition download, `getLogs`) stream as many small frames
  tagged with a request id, yielding between chunks so status polls and other
  commands aren't head-of-line-blocked during a multi-MB flash. (Explicitly
  chosen over accept-head-of-line-blocking.)
- **Relay: device dials out, dumb frame forwarder,** one socket per device.
  Exact mechanism undecided (dedicated relay vs. device reaching a server over a
  VPN that acts as the bridge).

## Auth model (decided so far)

- **Auth is a transport/connection concern, not a command property.** "Needs
  auth" depends on *how a command arrived*, not which command it is: `climateSet`
  over a physically-present serial cable is trusted; over a remote WS it must be
  gated. So auth lives at the connection edge (where Strux already puts it —
  commands never see a token). A per-command `requiresAuth` flag was considered
  and **rejected** for this reason.
- **Session = socket lifetime.** The WS connection *is* the session: open
  unauthenticated, a short pre-auth window accepts only `login`, success marks
  the socket authenticated for its life, close ends it. No token table, no token
  in the URL, no per-frame token validation, no timeout sweeper — less code than
  today's `SessionTable` dance.
- **`login` becomes a command**, owned/registered by the web transport, so the
  password-check *mechanism* is reusable by any future gated transport. But the
  *decision to gate* stays transport-owned. `/api/login` (POST + the open GET for
  device name) is removed.
- **Physically-trusted transports need no auth.** A local stdout/serial command
  console marks its connections authenticated from birth (norm for embedded
  consoles — you don't password a cable).

## Open questions

- **Relay/server design.** Dedicated dial-out relay vs. VPN-to-a-bridge-server.
  Not yet investigated.
- **Does the server/relay itself use "the WebSocket way" for its own auth?**
  The bridge server will need auth too; whether it reuses this same in-band
  WS/login model is undecided.
- **Binary request-id framing.** How to correlate binary chunks (firmware
  up/download) to a request on a shared socket — a text control frame `{id,type}`
  then id-tagged binary frames, or a length/id header on each binary frame.
- **Multiplexing details.** Chunk size, backpressure, and fairness so a large
  download can't starve interactive commands.
- **Pre-auth surface.** Which commands are allowed before login (`login`, and a
  `hello`/device-name), and the pre-auth window length.
- **Device-name bootstrap.** The login page needs the device name before auth:
  a pre-auth `hello` WS message vs. baking it into the static bootstrap.
- **TLS / relay trust.** Login is now an in-band frame, so the tunnel must be
  `wss`. Prefer a relay that stays a dumb pipe (end-to-end to the device) over
  one that terminates TLS. (Not a regression — token-in-URL has the same exposure
  today.)
- **stdout/console command transport** is its own idea; relates to
  `command-page.md`, `ui-command-driven.md`, `kc-command-tunnel.md`.

## Build/backport plan

Develop **here** (real device on COM13, live WS load, the reproduction), then
**backport to Strux**. The WS-transport files (`WebSocketHandler.*`, the command
dispatch in `WebServerManager`, `frontend/src/lib/backend.ts`) are verbatim Strux
in this repo, so the change is a near-clean cherry-pick *if* the WS-core work is
kept in isolated commits touching only template files, with no thermostat-specific
entanglement. This is its own brainstorm → spec → plan cycle.

## Immediate stopgap (separate, small)

Independent of this redesign: point `backend.getLogs()` at the existing HTTP
`/api/command` route (which already streams unbounded via `httpd_resp_send_chunk`)
instead of the WS `send()`. Frontend-only, no reflash, unblocks the Console page
today. `getLogs` moves back onto the WS for free once multiplexing lands.
