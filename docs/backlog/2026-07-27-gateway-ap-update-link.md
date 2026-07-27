# Firmware updates over the gateway's WiFi AP

**Status: direction chosen, not started.** (logged 2026-07-27) Supersedes
`2026-07-27-wifi-update-ui.md` (pull-a-pinned-build-from-GitHub) and the BLE
variants explored alongside it.

The gateway runs in **APSTA**: STA on the house WiFi (or its Ethernet uplink) and
an **AP** for the thermostat at the same time. The thermostat joins that AP as a
normal WiFi client, and the gateway drives the thermostat over the **WebSocket
command surface that already exists** — the same one the React frontend uses.
The gateway fetches images from KC servers over its own uplink and streams them
into `writePartition`.

## Why this over the alternatives

- **Almost no thermostat-side work.** The WS transport, `AuthGate`, and streamed
  `writePartition` all ship today. The gateway's AP is just another SSID in the
  WiFi screen that already exists. Contrast BLE: a `SessionLink` extraction (today
  `Session` hard-holds a concrete `WsSessionLink&`), a GATT service, bonding and a
  passkey UI.
- **~1.6 MB in seconds, not minutes.** WiFi TCP on ESP32 runs ~1–3 MB/s; BLE
  capped at ~20–40 KB/s by the gateway's 1M PHY (1–2 minutes).
- **Pairing stops being a research problem.** Joining a known SSID is boring and
  solved. Every BLE variant died on "which gateway?" — OT carries no unique device
  id (ID 3 is a *manufacturer* MemberID, identical across KC gateways), the
  gateway has no local UI to pick a thermostat, and multiple gateways in RF range
  breaks any pairing-mode-plus-passkey scheme.
- **No certificates on the thermostat.** The image arrives from a local peer, so
  there's no CA bundle to expire in a unit that sits in a hallway for eight years —
  the concern that stopped the GitHub-pull direction.
- **Standalone stays honest.** Nothing KC-specific lands on the thermostat: it
  exposes the same generic commands to anyone. A third party points a browser or
  their own controller at it and pushes firmware the same way.

## Bonus: NTP without the house network

With NAPT on the gateway (`esp_netif_napt_enable()`, needs `CONFIG_LWIP_IP_NAPT`,
and its DHCP server must hand out a DNS server) the thermostat reaches the
internet through the gateway. Intended scope is **NTP only** — plain UDP, no
certificates. Fixes clock-dependent behaviour without depending on the customer's
WiFi. Deliberately *not* a licence for the thermostat to fetch its own firmware
again; "has internet" and "is trusted to fetch firmware" stay separate decisions.

## Open questions

- **Does the thermostat also need APSTA?** Its single STA on the gateway's AP means
  it's off the house network, so the web UI isn't reachable from the LAN. Either it
  runs APSTA too, or it only joins the gateway during updates.
- **How does the gateway know a thermostat has arrived?** It sees a new DHCP client
  and tries the WS handshake, or the thermostat announces itself (mDNS already
  runs). Undecided.
- **AP credentials.** Per-gateway PSK vs. a shared default; where the installer
  reads it. This is where the security actually lives now.
- **Same-channel constraint.** One radio: the AP is dragged to the STA's channel,
  and AP clients can hiccup when the STA drops. Fine for a push, worth knowing.
- **Signed images** remain worth doing independently — they make any transport
  untrusted, including a spoofed AP.

## Relations

- Tracked under **RA2-437**. Most of the work is **gateway-side** (APSTA, NAPT,
  fetch from KC servers, WS client speaking the session-mux framing) and belongs in
  `esp_gateway`.
- **No firmware screen on the display.** The update is triggered by the gateway,
  so there is nothing for someone at the unit to do. The GitHub-pull branch was
  deleted rather than parked; what was learned from it is in the reasoning note.
- Local `.bin` upload via `FirmwarePage` remains the offline floor for a standalone
  unit and depends on nothing here.
