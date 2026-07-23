# KC Thermostat — Roadmap

> Each backlog item below has a dated `docs/backlog/YYYY-MM-DD-<item>.md` entry
> with the detail.
> Jira carries only the big picture (RA2-395 points here); technical work
> lives in this repo. Per item: brainstorm → spec → plan → implement.

The repo was reset to pure Strux on 2026-07-06 (see
`docs/superpowers/specs/2026-07-06-strux-reset-design.md`). Everything below is
rebuilt deliberately on that baseline, one feature at a time, each through its
own brainstorm → spec → plan cycle. The old demo (branch
`feature/ot-thermostat-dropin`) is reference material, not a merge source.

## The deliverable this roadmap serves

The work is split into two Jira stories. **This roadmap covers the first one;**
the second is tracked separately and is out of scope here.

1. **[RA2-395](https://koolecontrolsdevelopment.atlassian.net/browse/RA2-395) —
   1:1 OpenTherm drop-in, installable on location.** A unit that can be fitted
   at a location as a drop-in for the third-party thermostat *and updated
   remotely*. Standard OpenTherm master (THR=1): heating with PID modulation,
   cooling demand, domestic hot water, boiler status/diagnostics, touchscreen +
   web UI (no MQTT/HA — the gateway owns smart-home integration), remote setpoint
   override. Plus **remote firmware update** — which needs a gateway-side change,
   so "no gateway changes" does *not* hold for that item — and hardware /
   wall-mounting sorted enough to install on site (gated by RA2-389). **Done
   when:** a thermostat can be fitted on site as a drop-in and updated remotely.
2. **[RA2-435](https://koolecontrolsdevelopment.atlassian.net/browse/RA2-435) —
   guest chooses mode (heat/cool) + KC extensions.** The "make it ours" version:
   customer picks heat/cool from the thermostat, custom KC control behaviour and
   KC features over OT (custom data-IDs / KC-frame tunnel, gated behind OT
   member-ID detection with fallback to standard OT), the gateway-side changes
   those need, and later external sensors / schedules / productization. Gated by
   RA2-396. **Separate deliverable — not planned in this roadmap;** each item
   gets its own brainstorm when RA2-435 is picked up.

---

## What a basic OT thermostat must do (Step 1 definition of done)

Derived from the DIYLESS reference yaml + the demo experience:

**Heating (CH)**
- Own the room setpoint locally (NVS-persisted, ~5–30 °C, 0.5 ° steps) and a
  Heat/Cool/Off mode. Frost-safe: "off" still keeps a minimum setpoint floor.
- Measure room temperature: built-in AHT20 with a calibration offset setting
  (board self-heating ≈ +7 °C, see RA2-389); architecture leaves room for an
  external/BLE sensor later (step 2).
- PID control loop → control setpoint `t_set` (OT ID 1), clamped 30–80 °C,
  zero-means-zero. Clean-room implementation (ESPHome is GPLv3 — reuse the
  algorithm and constants, never the source). Seed constants from the yaml:
  kp 0.77, ki 0.0005, kd 0, 10-sample output averaging; deadband ±0.5 °C with
  ki×0.15 and 15-sample averaging inside the band.
- CH enable bit driven by mode + demand (OT ID 0 master status).

**Cooling**
- Cool mode raises the **cooling-enable bit** in the master status (ID 0) —
  this is exactly what the gateway consumes (`OpenthermThermostat::GetCoolingRequest()`
  reads `cooling_active`; `HeatManager` has CoolingStandby/CoolingActive states).
  Actuation stays the gateway's job; on/off demand suffices (modulating cooling
  control, ID 8, only if ever needed later).
- Offer Cool only when the slave config (ID 3) advertises cooling support (the
  gateway sets that from its `smarthome.coolingSupportEnabled` setting).
- Show cooling status from the slave status bits.

**Remote setpoint override (drop-in requirement the demo missed)**
- The gateway actively pushes setpoints to the wired thermostat (reservations /
  smart-home): remote-override room setpoint (ID 9, function flags ID 100),
  with echo-detection + timeout on the gateway side. The thermostat must adopt
  the override, reflect it in ID 16, and let the user change it locally
  afterwards — otherwise reservations silently stop working vs. the
  third-party unit.

**Hot water (DHW)**
- DHW enable/disable (OT ID 0), persisted.
- DHW setpoint (OT ID 56), 30–80 °C, default 60, persisted.
- DHW temperature readout (OT ID 26).

**Boiler status & diagnostics (read from slave)**
- Flame on, CH active, DHW active, fault/diagnostic indication (ID 0).
- Relative modulation (ID 17), boiler water temp (ID 25), return temp (ID 28),
  CH water pressure (ID 18), outside temp (ID 27) when available.
- OEM fault code (ID 5) + OEM diagnostic code (ID 115); the service-flag bits
  (service required, lockout, low water pressure, flame fault, air pressure,
  overtemp).
- Max `t_set` bounds from the boiler (ID 57 / 49) respected as clamps.

**OT master loop & link health**
- Master polling loop: status (0) every ~1 s; `t_set` (1); room setpoint (16)
  and room temp (24) so the gateway's own view stays populated; periodic reads
  of the sensor/diagnostic IDs above.
- STM32 co-processor link supervision: 900 ms warm-up, timeout → reconnect,
  visible link-state, and a safe state (no heat demand) when the link is down.

**UX**
- LVGL touchscreen UI: room temp + setpoint control, mode (Heat/Cool/Off),
  DHW on/off + setpoint, flame/heating/cooling/DHW activity, fault indication,
  backlight/screen timeout.
- Web UI page(s) with the same controls + the diagnostics.
- No MQTT / Home Assistant: those Strux managers are removed from this product
  (the gateway owns smart-home integration).

---

## Step 1 backlog (build order; each = one `docs/backlog/` item → spec → plan)

| # | Backlog item | Notes |
|---|--------------|-------|
| 1 | ~~`diyless-board-target`~~ | **Done (2026-07-06, ff3b14b).** Minimal-boot scope: board folder + AHT20 (TemperatureSensor/HumiditySensor role interfaces) + 8 MB partitions + USB-JTAG console; verified on hardware. ST7701/GT911 drivers deferred to `thermostat-ui`; CI to `release-workflow`. |
| 2 | ~~`opentherm-link`~~ | **Done (2026-07-07, 8c5c5cb, with item 3).** `Stm32OpenThermLink` ported behind the `OtLink` role interface; board owns reset + handshake. Gotcha: STM32 ResponseStatus byte is diagnostic-only (fwVer=1 returns 1 on success) — replies validated by parity + type + ID instead. esp32_devkit board dropped; DIYLESS T3 is the product. |
| 3 | ~~`opentherm-master-manager`~~ | **Done (2026-07-07, 8c5c5cb).** 500 ms master loop, full ID schedule (writes 1/16/24/56, reads 3/5/17/18/25/26/27/28/49/115), ID 9 override adoption + ID 16 echo, link supervision (6 fails → down, backoff recover), `otSet`/`otStatus` bench commands. Gateway E2E verified: heat call drives `HeatManager`, override round-trips via KC1 `CTMP SET`, wire pull → link down/up recovery. |
| 4 | ~~`room-temperature`~~ | **Done (2026-07-07).** `RoomTemperatureManager`: own task samples AHT20 every 5 s, 30 s validity, `roomTemp` command; OpenThermManager pulls ID 24 from it. Fixed a pre-existing `Aht20Sensor` bug (frozen value after post-boot I2C failure → now recovers + expires after 3 fails). Calibration split to `2026-07-07-room-temp-calibration.md` (offset vs curve undecided); humidity deferred to `thermostat-ui`. Verified: `roomTemp` valid on hardware, gateway sees live ID 24. |
| 5 | ~~`climate-pid`~~ | **Done (2026-07-07).** `ClimateManager`: Heat/Cool/Off + setpoint ownership (NVS-persisted), clean-room deadband PID (kp 0.77/ki 0.0005/kd 0, ±0.5 band, 10/15-sample averaging) → t_set into [maxTSetLower,maxTSetUpper], on/off cooling gated on `coolingSupported`, frost-safe Off (5 °C), ID 9 override adoption, safe-state on sensor fault, `climateSet`/`climateStatus`. OpenThermManager slimmed to transport (`SetHeatingDemand`/`SetDhwDemand` slices; ID 9 exposed raw, not adopted). Verified on hardware: heating curve, cooling, off, override round-trip, reboot persistence. |
| 6 | ~~`hot-water`~~ | **Done (2026-07-07).** `HotWaterManager`: DHW enable + setpoint (NVS-persisted, 30–80/default 60), no control loop (boiler owns tank), pushes `SetDhwDemand`, `hotWaterSet`/`hotWaterStatus` with live readout from `OtBoilerState`. `otSet` removed (all demand now owned by climate/hot-water). Verified: set → gateway ID 0 DHW bit + ID 56, reboot persistence. Deferred (see `2026-07-07-dhw-advanced.md`): legionella protection, DHW scheduling. Web exposure → item 8. |
| 7 | ~~`thermostat-ui`~~ (minimal) | **Done (2026-07-07).** Scoped down to first-light: ST7701 + GT911 + LVGL bring-up (drivers ported from the demo; panel init before OT link — GPIO11/12 handoff verified, OT stays linked), `DisplayManager` with a minimal screen (big room-temp number + − / + buttons nudging the setpoint ±0.5 via `ClimateManager::NudgeSetpoint`, `SET` shown ~4 s then reverts). Verified on hardware: panel + touch up, buttons respond. **Deferred to a fuller UI item:** mode/DHW/activity/fault/arc, screen timeout, backlight dimming, knob. Command-driven-UI idea parked (`2026-07-07-ui-command-driven.md`). |
| 8 | ~~`web-integration`~~ | **Done (2026-07-10).** Thermostat + Diagnostics web pages on Strux's WS session transport. `ThermostatPage`: room temp, setpoint ±0.5 nudge (debounced), Heat/Cool/Off (Cool gated on `coolingSupported`), DHW enable + setpoint, live Active/t_set/PID/flame/CH/DHW/override badges. `DiagnosticsPage`: boiler/return/DHW temps, modulation, CH pressure, outside temp, OEM fault/diag codes, t_set clamps, link/fault state. Polls `climateStatus`/`hotWaterStatus`/`otStatus`, writes via `climateSet`/`hotWaterSet`; login-over-WS + auto-reconnect via the singleton `backend`. Spec/plan `2026-07-09-web-integration-*`. Verified: typecheck + `pnpm build` clean on the new transport, `www` flashed and served (device returns the built asset hashes), all commands answer live. Browser walk-through is the remaining human check. Subsumes the old `frontend-rebuild-after-strux` note (removed). |
| 9 | ~~`dropin-validation`~~ | **Done (2026-07-10) — Step 1 complete.** Validated end-to-end against the gateway (THR=1, boiler emulator — no real boiler yet): gateway sees room temp + setpoint (KC1 `DTMP ACT/SET` match OT ID 24/16); heat → `t_set`/CH-enable reaches the gateway (demand drops when room>setpoint, ~15 s PID settle); cool → cooling-enable → gateway `coolingActive`; remote override (gateway `CTMP` → ID 9) adopted and echoed in ID 16; DHW enable + setpoint (ID 0 bit + ID 56) reach the gateway; MXT max-setpoint cap round-trips ([RA2-431](https://koolecontrolsdevelopment.atlassian.net/browse/RA2-431)). Real-boiler sanity check still pending hardware. |

**Baseline trim (do first, small):**

| # | Backlog item | Notes |
|---|--------------|-------|
| 0 | ~~`remove-mqtt-ha`~~ | **Done (02dd8fa).** MQTT + Home Assistant managers removed — the gateway owns smart-home integration. |

**Infra track (parallel, small):**

| # | Backlog item | Notes |
|---|--------------|-------|
| I1 | `software-id` | Reserve **ID 28 (0x1C)** on the [Software ID's page](https://koolecontrolsdevelopment.atlassian.net/wiki/spaces/DEV/pages/430211074) (group "KC Thermostat", ESP32-S3, TCP/IP ✓, BLE ✓). Embed in firmware + report in version info. |
| I2 | `release-workflow` | Rework `release.yml` gateway-style: `VX.Y.Z` tags, `-DSOFTWARE_VERSION_*` from tag, artifact naming `MM_mm_pp_<ID>_KC_<name>`, factory/app/www bins. Open question: does the thermostat also need `.hex`/`.kczip` (service-tool formats)? |
| I3 | `branding` | Device name "KC Thermostat" (AP SSID, web title) over Strux defaults. |

**Open question — gateway-side identity (decision deferred, doesn't block Step 1):**
how does the gateway see our thermostat? Options: (a) it stays plain **THR=1
(OTH thermostat)** — which it *is* — and the gateway later detects "ours" via an
OT register (member-ID / product ID) to unlock expanded features; or (b) the
gateway gets a dedicated thermostat-type option (working name `CSHWTHR`), which
is more explicit configuration. Either way Step 1 works as a standard OTH
drop-in; revisit when KC extensions (Step 2) become real.

## Remaining for RA2-395 (this deliverable is not done yet)

The drop-in *software* is complete (all 9 backlog items above). What still
stands between here and "installable on location, updatable remotely":

- **Remote firmware update** — the field-update path. Not purely
  thermostat-side: it needs a gateway-side change to carry the update, so this
  is where "no gateway changes" stops holding. Needs its own brainstorm → spec
  → plan.
- **On-site mounting / hardware** — enclosure + wall mount sorted far enough to
  fit at a location. Gated by RA2-389 (DIYLESS/Ihor).
- **Real-boiler sanity check** — validated so far against the gateway boiler
  emulator only; still pending real hardware.

## KC extensions live in RA2-435, not here

Everything that makes the thermostat *ours* — customer-selectable heat/cool,
KC features over OT (custom data-IDs / KC-frame mailbox, gated behind member-ID
detection, blocked by RA2-396), external/BLE room sensors, schedules/programs,
productization beyond mounting — is scoped under
[RA2-435](https://koolecontrolsdevelopment.atlassian.net/browse/RA2-435) and is
**deliberately out of scope for this roadmap.** Each item gets its own
brainstorm when that story is picked up; the technical breakdown will land in
this repo then.
