# Climate control (PID) — Design

**Date:** 2026-07-07
**Status:** Approved
**Backlog item:** `docs/backlog/climate-pid.md` (Step 1, item 5)

## Goal

The thermostat owns its control logic: a `ClimateManager` holds mode
(Heat/Cool/Off) and setpoint, runs a heating PID that produces the OpenTherm
control setpoint (`t_set`), issues on/off cooling demand, and pushes it all
to `OpenThermManager` — which becomes a pure transport. A setpoint step on
the bench yields a plausible modulating `t_set` curve; a dead room-temp
sensor degrades to no demand.

## Licensing

The PID algorithm and constants are reused from DIYLESS's
`diyless-thermostat-3.yaml` (ESPHome climate PID). **ESPHome is GPLv3 — the
algorithm and numeric constants are reused, the source is not.** `PidController`
is a clean-room reimplementation.

## Decisions

- **`ClimateManager`** — new manager, standard Strux pattern (`ServiceProvider&`
  ctor, copy/move deleted, `InitState`-guarded `Init()`, own `Task`).
  Init order: after `RoomTemperatureManager` and `OpenThermManager` (it
  consumes both), before `UpdateManager`/`WebServerManager`.
- **`PidController`** — separate header-only class (like `OtFrame.h`), pure
  logic, no ESP/manager dependencies. Holds the integral accumulator and the
  output-averaging ring; `float Update(float setpoint, float measured, float dtSeconds)`
  returns a heat-demand output in `[0, 1]`.
- **Control loop:** ClimateManager's task runs every **5000 ms** (aligned
  with `RoomTemperatureManager`'s sampling). The PID integral is scaled by
  the actual elapsed `dt` in seconds (measured via `esp_timer_get_time()`),
  not a fixed constant — so a late tick doesn't distort the integral.
- **Mode + setpoint persistence:** TypedSettings (NVS). Setpoint range
  **5–30 °C**, resolution 0.5 °C; mode is `Off=0 / Heat=1 / Cool=2`.

## Control law (heating)

`activeSetpoint` depends on mode: in **Off** it is always the frost setpoint
(a remote override is ignored — Off stays Off, frost-only); in **Heat/Cool**
it is the ID 9 override setpoint when active, otherwise the user setpoint.

- `error = activeSetpoint − roomTemp` (positive = too cold = wants heat).
- Constants: **kp 0.77, ki 0.0005 (per second), kd 0** (kd 0 ⇒ no
  derivative term at all).
- `output = clamp(kp·error + ki·integral, 0, 1)` — heat-only; negative
  results clamp to 0. `integral += error · dtSeconds` each tick, but only the
  proportional+integral sum is what matters (kd 0).
- **Deadband ±0.5 °C** around `activeSetpoint`:
  - Inside the band (`|error| ≤ 0.5`): integral gain reduced to **ki × 0.15**
    (anti-windup near target), and the returned output is the mean of the
    last **15** computed outputs.
  - Outside the band: full ki, output is the mean of the last **10** outputs.
  - Implementation: a 15-slot output ring; average over the active window
    width (10 or 15) depending on the current deadband state.
- **Integral clamp:** the integral term (`ki·integral`) is clamped to
  `[0, 1]` so windup cannot accumulate unbounded while the boiler saturates.
- **Output → t_set mapping** (the one item to validate on the bench):
  `lo = state.maxTSetLower` (default 30), `hi = state.maxTSetUpper`
  (default 80). If `output ≤ 0.01` → `t_set = 0` (zero-means-zero, boiler
  idles). Else `t_set = clamp(lo + output · (hi − lo), lo, hi)`.

## Modes → demand

Each cycle ClimateManager computes an `OtDemand` heating slice and pushes it
via `OpenThermManager::SetHeatingDemand(chEnable, coolEnable, roomSetpoint, tSet)`.
`roomSetpoint` pushed = `activeSetpoint` (so ID 16 echoes the override).

- **Heat:** `chEnable = true`, `coolEnable = false`. PID against
  `activeSetpoint`; `tSet` from the mapping above.
- **Cool:** on/off. `coolEnable = state.coolingSupported && (roomTemp >
  activeSetpoint + 0.5)`; `chEnable = false`; `tSet = 0`. PID not used for
  cooling (actuation/modulation is the gateway's job). If the boiler doesn't
  advertise cooling, `coolEnable` stays false.
- **Off:** frost-safe. Run the heating PID against a fixed **frost setpoint
  5.0 °C** (`kFrostSetpointC`). `chEnable = (tSet > 0)`, `coolEnable = false`.
  At normal room temperatures output is 0 ⇒ no demand; only a near-freezing
  room engages heat.

## Remote override (ID 9)

- `OpenThermManager` no longer mutates its own demand on an ID 9 read.
  Instead `OtBoilerState` gains `float overrideSetpoint` (0 = none); the ID 9
  read stores the clamped value there.
- ClimateManager reads `state.overrideSetpoint` each cycle. In Heat/Cool,
  nonzero → it is the `activeSetpoint` (adopted; a valid→different transition
  is logged), taking precedence over the user setpoint until it returns to 0.
  In Off it is ignored. The PID runs on the adopted value; the pushed
  `roomSetpoint` carries it, so the gateway sees ID 16 echo the override (the
  contract it waits on).
- Clamp adopted overrides to the setpoint range [5, 30] (same as today).

## Sensor-failure (safe state)

If `RoomTemperatureManager::GetRoomTemperature()` returns false (no valid
recent sample), ClimateManager pushes a safe demand: `chEnable=false`,
`coolEnable=false`, `tSet=0`; resets the PID integral and output ring; logs
the fault once (edge-detected). Recovery resumes normal control and logs
once. This is the consumer of the validity signal built in item 4.

## Commands (interim bench surface until item 7 UI)

- **`climateSet`** — JSON `{mode?, setpoint?}`. `mode` accepts
  `"off"|"heat"|"cool"` (or 0/1/2); `setpoint` clamped to [5, 30]. Persists
  changed values; replies with the resulting `climateStatus`.
- **`climateStatus`** — `{mode, userSetpoint, activeSetpoint, roomTemp,
  roomValid, pidOutput, tSet, chEnable, coolEnable, overrideActive}`.
- **`otSet` retires its heating writes:** the setpoint/ch/tset/cool fields
  are removed; it keeps only `dhw`/`dhwSetpoint` (interim, until item 6 gives
  DHW its own manager), routed through the new `SetDhwDemand(dhwEnable,
  dhwSetpoint)`. `otStatus` is unchanged (boiler diagnostics).

## OpenThermManager changes (slims to transport)

- `SetDemand(const OtDemand&)` → replaced by two slice setters:
  `SetHeatingDemand(bool chEnable, bool coolEnable, float roomSetpoint, float tSet)`
  and `SetDhwDemand(bool dhwEnable, float dhwSetpoint)`. Each updates its
  fields of the internal `demand_` under the lock and change-detects the
  dirty flag (preserving the rotation-jump contract). No other manager reads
  or writes `demand_`.
- `OtBoilerState`: add `float overrideSetpoint = 0;` exposed via
  `GetState()`; the ID 9 read populates it. Remove the demand-mutating
  `AdoptOverride` path.
- `GetDemand()` stays (diagnostics/echo). ID 24 room temp continues to be
  sourced from `RoomTemperatureManager` (both PID and OT see the same value);
  it is not routed through the demand struct.

## Files

| File | Content |
|---|---|
| `main/Application/ClimateManager/ClimateManager.h/.cpp` | Manager, 5 s loop, mode/setpoint state + settings, override adoption, safe state, commands. |
| `main/Application/ClimateManager/PidController.h` | Header-only clean-room PID with deadband + output averaging. |
| `main/Application/ClimateManager/ClimateMode.h` | `enum class ClimateMode { Off, Heat, Cool }` + parse/format helpers. |
| `main/Application/ServiceProvider.h` | `getClimateManager()` accessor + forward decl. |
| `main/Application/ApplicationContext.h` | Member (after OpenTherm) + accessor. |
| `main/main.cpp` | `Init()` after OpenTherm, before Update. |
| `main/CMakeLists.txt` | Source + include dir. |
| `main/Application/OpenThermManager/OpenThermManager.h/.cpp` | Slice setters, `overrideSetpoint` in state, ID 9 read stores it, `AdoptOverride` removed, `otSet` heating writes removed. |

## Verification

1. Build green; flash.
2. **Heating curve:** `climateSet {"mode":"heat","setpoint":26}` with room
   ~24 → `climateStatus` shows `pidOutput` rising and a `tSet` in [30, 80];
   gateway `HeatingActive`, KC1 `DTMP SET` tracks the pushed room setpoint.
   Lower setpoint below room → output falls to 0, `tSet` 0.
3. **Cooling:** `climateSet {"mode":"cool","setpoint":20}` with room > 20.5
   → `coolEnable:true` in `climateStatus` (requires gateway
   `smarthome.coolingSupportEnabled`); `chEnable:false`.
4. **Off / frost:** `climateSet {"mode":"off"}` → no demand at normal temps.
   Frost engagement (room < 5 °C) not bench-reachable — verified by review;
   optionally confirmed by temporarily raising `kFrostSetpointC` above room
   temp in a throwaway build and seeing heat demand appear.
5. **Override:** push a setpoint from the gateway (ID 9) → `climateStatus`
   shows `overrideActive:true` and `activeSetpoint` = the pushed value;
   gateway sees it echoed in ID 16.
6. **Sensor fault:** verified by review (can't unplug the soldered AHT20) —
   invalid room temp → safe demand, integral reset, logged once.
7. **Persistence:** set mode + setpoint, reboot, confirm both retained.

## Out of scope

- DHW logic (item 6 `hot-water`) — only the interim `otSet` dhw fields here.
- On-screen UI (item 7) and web climate page (item 8) — commands are the
  interim surface.
- Schedules/reservations, multi-setpoint programs (later).
- Calibration of room temp (item `room-temp-calibration`, deferred).
- Auto (heat-or-cool) mode — Heat/Cool/Off only for Step 1.
