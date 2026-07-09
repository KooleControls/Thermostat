# Web integration (Step 1, item 8) — design

Expose the thermostat's climate/DHW controls and boiler diagnostics through the
browser, on top of Strux's existing WebSocket command plumbing. No MQTT/HA (the
gateway owns smart-home integration).

## Scope

This is a **frontend-only** feature. Every control and readout it needs is
already a registered device command:

- `climateStatus` / `climateSet` — mode (off/heat/cool), user + active setpoint,
  room temp + validity, PID output, t_set, CH/cool enable, override-active.
- `hotWaterStatus` / `hotWaterSet` — DHW enable + setpoint, plus live
  `dhwActive` / `dhwTemp` / `dhwPresent` from the boiler.
- `otStatus` — full boiler diagnostics: `linked`, `fault`, `chActive`,
  `dhwActive`, `flame`, `coolingActive`, `dhwPresent`, `coolingSupported`,
  `boilerTemp`, `returnTemp`, `dhwTemp`, `modulation`, `chPressure`,
  `outsideTemp`, `oemFaultCode`, `oemDiagCode`, `maxTSetUpper`, `maxTSetLower`,
  `overrideSetpoint`, plus our demand (chEnable/dhwEnable/coolEnable/roomSetpoint/
  dhwSetpoint/tSet).
- Settings (calibration offset, clamps, timeouts) already appear in the
  generated Settings UI via `TypedSettings`, so they are out of scope here.

No firmware changes. No new device commands.

## Live-update mechanism: polling

Pages fetch live state by **polling the existing status commands only while
mounted** — an effect starts an interval on mount and clears it on unmount, the
same shape as `use-device-info.ts` (Home's 10 s device-info poll).

Polling is the right call here because these frames only travel while a browser
tab is actually sitting on the Thermostat or Diagnostics page — in practice,
during commissioning/testing, effectively never in normal operation. Push would
be optimizing traffic that mostly does not happen, at the cost of firmware work.

Cadence: Thermostat page ~2 s (responsive when nudging a setpoint), Diagnostics
~5 s (boiler values move slowly).

The Thermostat page polls three commands — `climateStatus` and `hotWaterStatus`
for the controls, plus `otStatus`, because the boiler-reported bits it displays
live only there: `coolingSupported` (gates whether the Cool button appears) and
`flame` / `chActive` / `dhwActive` (the activity badges). `climateStatus` only
carries our *demand* bits (chEnable/coolEnable), not the boiler's actual state.

### Future upgrade path (deferred, not built)

If live push is ever wanted, the natural home is a dedicated
**`MonitoringManager`**: it owns a timer, reads the domain managers via the
service locator, and calls the already-public `WebServerManager::Broadcast()`.
The dependency then points Monitoring→domain and Monitoring→web — never
web→domain (a `WebServerManager` that reaches into `ClimateManager` etc. is
explicitly rejected). Broadcasts ride the existing "no `id` field" frame that
`backend.subscribe()` already dispatches (the log stream uses it today).

Because each page's data lives behind a single hook, swapping poll→subscribe
later is an internal change to that one hook, with no page rewrites. The hooks
are the seam that keeps this option cheap.

## Architecture

### Backend service wrappers

`frontend/src/lib/backend.ts` gains thin typed wrappers over `send()` plus reply
interfaces mirroring the command JSON shapes: `getClimateStatus()` /
`setClimate({mode?, setpoint?})` returning `ClimateStatus`; `getHotWaterStatus()`
/ `setHotWater({enable?, setpoint?})` returning `HotWaterStatus`;
`getOtStatus()` returning `OtStatus`. Each `*Set` reply carries the full fresh
status, so a set doubles as a read and the UI reconciles from the reply.

### Hooks (the data seam)

`useThermostatStatus()` polls `climateStatus` + `hotWaterStatus` + `otStatus` on
the ~2 s interval and exposes the merged state plus setter functions that issue
the set commands and reconcile from their replies (`otStatus` supplies
`coolingSupported` and the boiler activity bits the page shows).
`useDiagnostics()` polls `otStatus`
on the ~5 s interval and exposes the boiler/link state read-only. Both gate on
`useConnectionStatus()` and early-return unless connected. These hooks are the
only place that knows *how* data arrives — the future push swap happens here.

### Pages and routing

Two default-exported page components under `frontend/src/pages/`:
`ThermostatPage.tsx` and `DiagnosticsPage.tsx`. Wiring them into the hand-rolled
router touches the three established places: a `navItems` entry each in
`AppSidebar.tsx` (Lucide `Thermometer` and `Stethoscope`/`Activity` icons), the
page strings in `validPages` in `use-route.ts`, and a `case` each in
`PageContent` in `App.tsx`. Both pages render inside the authenticated shell
automatically; no extra auth work.

The existing Home page is left unchanged.

## Thermostat page

- **Hero (mirrors the on-device LCD):** large room-temperature number; the
  setpoint with −/+ buttons (0.5 ° steps, clamped 5–30). Room temp shows "—"
  when `roomValid` is false. No slider component is added — steppers match the
  LCD and avoid pulling in new shadcn parts.
- **Mode:** three segmented buttons Off / Heat / Cool. Cool is hidden unless
  `otStatus.coolingSupported` (the boiler advertised cooling in ID 3).
- **DHW:** an enable `switch` plus setpoint −/+ (1 ° steps, clamped 30–80).
- **Readout strip:** active setpoint, t_set, PID output, and badges for flame /
  CH-active / DHW-active and override-active.

The web page is inherently command-driven (the browser can only reach the device
through commands). Making the *device LCD* also command-driven — so screen and
web share one control path, with eventual LVGL-in-WASM reuse — is a device-side
refactor that overlaps the undecided KC-command rework and stays parked
(`ui-command-driven.md`). It does not block this page.

## Diagnostics page

Read-only. Grouped cards of label/value rows (plain layout — no table component
needed):

- **Sensors:** boiler temp, return temp, DHW temp, relative modulation %, CH
  water pressure, outside temp.
- **Diagnostics:** OEM fault code, OEM diagnostic code.
- **Clamps:** t_set upper / lower bounds reported by the boiler.
- **Link / status:** a badge for `linked` (up/down), `fault`, and
  cooling-support. Values render "—" when the link is down.

## Interaction and write-hygiene

Every `*Set` command persists to NVS on the device, so a burst of −/+ clicks must
not become a burst of NVS writes. Setpoint changes (room and DHW) update local
state instantly for a snappy feel but the `climateSet` / `hotWaterSet` call is
**debounced ~500 ms**, so a rapid sequence collapses to a single write of the
final value. Mode buttons and the DHW enable toggle are discrete single actions
and send immediately.

Local pending state reconciles against the command reply and the next poll, so
the displayed value converges on the device's authoritative (clamped) value.

## Error handling

Command failures surface via `toast.error` (the existing sonner pattern).
Polling errors are logged but **not** toasted, to avoid a toast every couple of
seconds on a flaky link; the connection-status hook already signals the outage.
Before the first data arrives, pages render a "Connecting…" placeholder (the
HomePage `!info` guard pattern). Stale/invalid data is shown explicitly — "—"
for an invalid room reading or a downed link, a "link down" badge on Diagnostics.

## Verification

There are no automated tests in this repo (per `CLAUDE.md`); verification is
building and driving a device:

1. `cd frontend && pnpm typecheck && pnpm build` (the build gzips into `www/`).
2. Flash and open the web UI against a device on the OT link to the gateway
   (THR=1).
3. Drive it: nudge the setpoint → the LCD and the gateway's view reflect it;
   switch mode Off/Heat/Cool; toggle DHW and change its setpoint; confirm the
   Diagnostics page shows live boiler values and tracks the OT link going
   down/up.

This exercise flows directly into item 9 (drop-in validation).

## Out of scope / deferred

- **Push/broadcast + `MonitoringManager`** — future seam described above; hooks
  keep the swap cheap.
- **Device-LCD command-driven retrofit + LVGL-in-WASM** — parked
  (`ui-command-driven.md`).
- **PID tuning UI** — the Settings UI already exposes kp/ki/kd and deadband.
- **Generic command page** — separate backlog item (`command-page.md`).
