# UI for connecting to WiFi

**Status: done on the display, confirmed working on the unit.** (updated 2026-07-27)
A way for someone standing at the unit to put the thermostat onto a WiFi network —
scan/select an access point and enter its password — so it can reach the internet
(or a firmware host).

This is the connectivity prerequisite for the interim WiFi update
(`2026-07-27-wifi-update-ui.md`): in the field a technician joins the thermostat
to *some* AP (a phone hotspot, house WiFi, whatever is available) and the device
does the rest.

## What landed

Settings → **WiFi** (the row shows the current SSID) opens a screen that scans,
lists the networks strongest-first, and takes a passphrase on an on-screen
keyboard. Open networks connect straight from the list with no prompt. The status
line reports scanning / connecting / connected-with-IP / fallen-back-to-own-AP.

Two things had to change underneath:

- **The scan runs on a worker task.** `esp_wifi_scan_start()` blocks for seconds,
  which would freeze the panel, so `WifiScanner` scans off the LVGL task and the
  screen collects results from its refresh timer.
- **`NetworkManager::ConnectToStation()` is new** — credentials used to be read
  only at boot, so changing networks meant a reboot. It persists to
  `wifi.ssid`/`wifi.password` and reconnects immediately. The AP-fallback path
  (3 attempts, then own AP) is untouched, and reconnect-after-reboot still works
  because it writes the same settings boot already reads.

Reasoning: `docs/reasoning/2026-07-27-12h18-display-service-menu-shell.md` for the
menu shell this sits in, `2026-07-27-13h06-navigator-carries-no-payload.md` for why
the passphrase pad is a view inside this screen, and
`2026-07-27-13h06-2-runtime-wifi-credentials.md` for the connect path.

## Still open

- **Wrong-password feedback is indirect.** A bad passphrase shows as three
  connect attempts and then "own access point" rather than "wrong password" —
  `WIFI_EVENT_STA_DISCONNECTED` carries a reason code that `NetworkManager`
  currently drops. Worth surfacing.
- **Web UI has no matching page.** Still only the generic settings rows for
  `wifi.ssid`/`wifi.password`; a `SettingsPage` WiFi panel is a cheaper second
  surface if wanted.
- **No hidden-network entry.** Nameless scan results are filtered out of the list
  and there's no "join other network" affordance to type an SSID by hand.

## Relations

- Prerequisite for `2026-07-27-wifi-update-ui.md` (the update needs internet) —
  that item plugs into the same menu, which now exists.
- Keep the control surface generic (boundary rule — no KC specifics here).
