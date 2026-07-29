# Firmware updates over the gateway's WiFi AP — thermostat side

> **SUPERSEDED 2026-07-29 — the management link pivoted to BLE.** See
> `2026-07-29-ble-gateway-link.md`,
> `docs/reasoning/2026-07-29-09h17-ble-instead-of-the-gateway-ap.md` and its correction
> `-09h38-the-ble-pivot-is-about-parallel-availability.md`. Kept because the steps below
> that are not about the AP still stand and moved to the BLE doc.

**Status: the AP link worked on hardware; the direction is abandoned.**
(updated 2026-07-27, superseded 2026-07-29) Superseded the GitHub-pull and BLE
directions in turn.

The gateway hosts a WiFi AP, the thermostat joins it as a client, and the gateway
drives the thermostat over the **WebSocket command surface that already exists** —
streaming into `writePartition`. The gateway fetches the image itself from a plain
HTTP URL handed to it over KC2, verified against a SHA-256 from that same channel.

**Verified on hardware 2026-07-27:** the thermostat associates with the gateway's
`KC1245-<gatewayId>` AP (WPA2) and takes a DHCP lease — 192.168.4.2, gateway
192.168.4.1.

Reasoning: `docs/reasoning/2026-07-27-14h08-gateway-ap-instead-of-ble.md` and
`-14h08-2-ntp-through-gateway-not-firmware.md`.

## Most of the work is on the gateway

The transport, auth and streamed `writePartition` all ship here already, so this
repo needs almost nothing. The gateway plan — `CFUP <target> <url> <sha256> <size>`,
the WebSocket client, the streaming fetch — lives in
`esp_gateway/docs/plans/2026-07-27-thermostat-firmware-push.md`.

## Thermostat-side steps

1. **Merge `feature/wifi-diagnostics`.** Logs the WiFi disconnect reason code and the
   passphrase length. Not cosmetic: diagnosing the first real join failure required
   flashing it in — reason 210 (no AP with compatible security) is what told us the
   stored passphrase was empty rather than wrong.
2. **Fix lost-IP recovery.** `Ipv4Lost` clears `staConnected_` but nothing retries,
   and the link-down path only reconnects if it *was* connected — so a lease lost
   while still associated strands the device until reboot. Hit this for real during
   testing. Blocks any claim of unattended updates.
3. **Decide whether the thermostat also runs APSTA.** Its single STA on the gateway's
   AP takes it off the house network, so its own web UI becomes unreachable from the
   LAN (NAT does not help — outbound only). Either it runs its own AP alongside, or
   it joins the gateway only when there is work to do.
4. **Joining the AP unattended.** A human typed the credentials on the touchscreen.
   Field updates need the SSID and PSK known in advance — shared default, derived
   from the gateway id, or exchanged some other way. This is the last remnant of the
   pairing problem and it is a design decision, not code.
5. **Consider setting `web.password`.** It defaults to empty, meaning auth off, so
   anyone on the AP can call `writePartition`. If it is set, the gateway needs the
   credential and something has to provision it.
6. **NTP through the gateway** (optional, independent): with NAPT on the gateway the
   thermostat gets internet for NTP only — plain UDP, no certificates — so the clock
   stops depending on the customer's WiFi. Deliberately not a licence to fetch its
   own firmware.

## Relations

- Tracked under **RA2-437**.
- The `www` partition is a second transfer over the same mechanism, not new work
  here — a release ships `Thermostat-<v>-www.bin` and an app-only push leaves the
  frontend behind.
- Local `.bin` upload via `FirmwarePage` stays the offline floor for a standalone
  unit and depends on none of this.
