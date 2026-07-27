# UI for updating firmware over WiFi

**Status: not started — the interim remote-update path.** (logged 2026-07-27)
Trigger a firmware update from the unit: pull a **pinned** build from a server
over WiFi and apply it. This is step 1 of the RA2-437 staging (on-visit WiFi
pull, local trigger) — shipping now to keep progress while the full update
design is discussed with Mark. See the RA2-437 options brief.

Because it drives the generic `updateFromUrl` command, it's a **subset** of any
final design (BLE / OT-token orchestration add remote *triggers* later without
changing this), not throwaway.

## What already exists

- Device side: `updateFromUrl` (device pulls an image from an HTTP URL into the
  next OTA slot), A/B slots with rollback, `writePartition`.
- Web side: `FirmwarePage` already lists partitions and uploads a **local** `.bin`
  with a progress bar (the offline floor). What's missing is the **pull-from-server**
  trigger and version pinning.
- Display side (2026-07-27): the **service menu exists** — gear on the home
  screen, `ui.pin` gate (default `0000`), and a **disabled "Firmware" row** waiting
  for this screen. Adding one is: a `Screen` subclass, a `ScreenId`, a member on
  `DisplayManager`, and enabling the row. Reasoning:
  `docs/reasoning/2026-07-27-1218-display-service-menu-shell.md`.

## Likely scope

- A **"pull a specific version" trigger** — in a **hidden service menu on the
  display** (the primary interim surface) and/or on `FirmwarePage`. Calls
  `updateFromUrl` against the next OTA slot, shows progress, then apply/reboot.
- **Version pinning is a hard requirement** — never "just latest." Decide the
  version source (plain URL field vs. base-URL+version vs. a manifest/dropdown);
  a URL/version field is enough for the interim.
- Somewhere to **host the demo `.bin`** — release artifact naming exists, but the
  release workflow / hosting is ad-hoc for now (temp static host or internal
  server) until the release-workflow item lands.

## Relations

- Depends on `2026-07-27-wifi-connect-ui.md` (needs the unit on a network) and
  shares the display service-menu shell with it.
- Tracked under **RA2-437**; the generic command surface keeps the boundary rule
  (KC meaning stays on the gateway).
- Later remote triggers (OT version-register token, BLE) reuse this verbatim.
