# Rebuild the frontend for the new WS session transport

**Status: required follow-up to the Strux session-transport merge (2026-07-10).**

The Strux merge switched the device to the WebSocket session-multiplexed
transport and **retired `/api/command` + `/api/login`**. The firmware and our
command surface are verified over the new transport, and the frontend *source*
(`backend.ts`, `AppSidebar.tsx`) was resolved to the new transport during the
merge — **but the committed `www/` (the flashed web page) is still the old
HTTP-based build.** So the browser UI won't work correctly until the frontend is
rebuilt and reflashed.

## To do

- `cd frontend && pnpm install && pnpm typecheck && pnpm build` (needs pnpm).
  `pnpm build` gzips into `../www` (the FAT image embedded in flash).
- Fix any typecheck fallout from the merge resolution — likely spots:
  - `backend.ts`: confirm the thermostat methods (`getClimateStatus`,
    `setClimate`, `getHotWaterStatus`, `setHotWater`, `getOtStatus`) still
    compile against the reworked `send()`; the obsolete `commandUrl`/
    `getLoginInfo` were dropped — make sure nothing still calls them
    (e.g. `LoginPage`).
  - `AppSidebar.tsx`: `useEffect` import kept (it is used); confirm the
    Thermostat/Diagnostics nav items still resolve to real pages.
- Reflash `www` (independent FAT/OTA partition) and load `http://<thermostat-ip>`
  to confirm login-over-WS + the pages work.

## Why deferred

Merged to `main` firmware-first (everything device-side is green over the WS
transport). The frontend rebuild needs `pnpm` and a browser check — a separate,
self-contained pass. Not blocking further firmware work.
