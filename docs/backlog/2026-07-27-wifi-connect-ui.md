# UI for connecting to WiFi

**Status: not started.** (logged 2026-07-27) A way for someone standing at the
unit to put the thermostat onto a WiFi network — scan/select an access point and
enter its password — so it can reach the internet (or a firmware host). Needs its
own brainstorm → spec → plan when picked up.

This is the connectivity prerequisite for the interim WiFi update
(`2026-07-27-wifi-update-ui.md`): in the field a technician joins the thermostat
to *some* AP (a phone hotspot, house WiFi, whatever is available) and the device
does the rest.

The **backend already exists** — Strux provides WiFi STA plus an AP fallback and
NVS-persisted credentials. This item is the **front-end** to drive it: scan,
pick, enter key, show connection state.

## Likely scope

- **Where it lives** — the field-relevant surface is the **display** (a screen in
  the service menu, see `2026-07-27-wifi-update-ui.md`), since installation
  happens at the unit. A matching page on the web UI (`SettingsPage` already
  exists) is a cheaper second surface; decide whether both are in scope now.
- Scan visible APs, select one, enter the passphrase (on-screen keyboard on the
  display), trigger connect, show connected/failed + the current IP/SSID.
- Persist so it reconnects after reboot; keep the AP-fallback path intact for
  when no STA is configured.

## Relations

- Prerequisite for `2026-07-27-wifi-update-ui.md` (the update needs internet).
- Shares the display service-menu shell with the update UI — build the menu once.
- Touches `SettingsPage` on the web side; keep the control surface generic
  (boundary rule — no KC specifics here).
