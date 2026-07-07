# Domestic hot water (DHW) — Design

**Date:** 2026-07-07
**Status:** Approved
**Backlog item:** `docs/backlog/hot-water.md` (Step 1, item 6)

## Goal

A `HotWaterManager` that owns DHW enable + setpoint (persisted), pushes them
to the boiler over OpenTherm, and exposes DHW status for the UI. This takes
DHW ownership off the interim `otSet` path — after which `otSet` is removed
entirely, since every demand slice now has a dedicated owner.

## Reference (DIYLESS `diyless-thermostat-3.yaml`)

Confirmed the shape and defaults (reused as behaviour reference, not source
— ESPHome is GPLv3): DHW enable is a switch defaulting **OFF** (persisted);
DHW setpoint is **30–80 °C, step 1, default 60** (persisted); DHW temperature
comes from ID 26; a DHW-active binary sensor reflects the status bit. There
is **no ID 48** (DHW setpoint bounds) in their config — so a fixed 30–80
range is the reference behaviour, not a shortcut.

## Decisions

- **`HotWaterManager`** — new manager, standard Strux pattern
  (`ServiceProvider&` ctor, copy/move deleted, `InitState`-guarded `Init()`).
  Init order: after `OpenThermManager` (it calls `SetDhwDemand`), before
  `UpdateManager`.
- **No periodic task.** DHW has no control loop — the boiler owns tank
  heating; the thermostat only states "DHW enabled, setpoint X". The manager
  pushes on two occasions: once in `Init()` (after loading persisted values)
  and again whenever a command changes them. `OpenThermManager`'s rotation
  keeps re-sending the DHW-enable bit (ID 0) and ID 56 from its stored demand.
- **Owned state (persisted, TypedSettings/NVS):**
  - `dhw.enable` — `BoolSetting`, default `false`.
  - `dhw.setpoint` — `FloatSetting`, default `60.0`. Clamped to **[30, 80]**;
    step is a UI concern, not enforced here.
- **Readout is pulled live, not mirrored.** DHW status comes from
  `OpenThermManager::GetState()` at command time: `dhwActive`, `dhwTemp`
  (ID 26), `dhwPresent` (ID 3 config bit). No duplicated state in this
  manager beyond its own enable/setpoint.
- **Fixed 30–80 bounds** (no ID 48 read), matching DIYLESS.
- **No gating on `dhwPresent`** — enable is sent regardless and `dhwPresent`
  is reported for the UI, same as DIYLESS's plain switch. A boiler without
  DHW simply ignores it.

## Commands (interim bench surface until item 7 UI)

- **`hotWaterSet`** — JSON `{enable?, setpoint?}`. `enable` accepts 0/1;
  `setpoint` clamped to [30, 80]. Persists changed values (`SettingsManager::Save()`),
  pushes `SetDhwDemand`, replies with `hotWaterStatus`.
- **`hotWaterStatus`** — `{enable, setpoint, dhwActive, dhwTemp, dhwPresent}`.

## OpenThermManager change

- **Remove `otSet` entirely** — its command-table entry, the `Cmd_Set`
  handler, and (already gone) its helpers. DHW was its last remaining
  function; all demand is now owned by `ClimateManager` (`climateSet`) and
  `HotWaterManager` (`hotWaterSet`).
- `otStatus` stays (boiler diagnostics). `SetDhwDemand` stays — now called
  only by `HotWaterManager`. `GetDemand()` stays (used by `otStatus`).

## Files

| File | Content |
|---|---|
| `main/Application/HotWaterManager/HotWaterManager.h/.cpp` | Manager: two persisted settings, push-on-change + push-on-init, `hotWaterSet`/`hotWaterStatus`. |
| `main/Application/ServiceProvider.h` | `getHotWaterManager()` accessor + forward decl. |
| `main/Application/ApplicationContext.h` | Member (after OpenTherm) + accessor. |
| `main/main.cpp` | `Init()` after OpenTherm, before Update. |
| `main/CMakeLists.txt` | Source + include dir. |
| `main/Application/OpenThermManager/OpenThermManager.h/.cpp` | Remove `otSet` command + `Cmd_Set`. |

## Verification

1. Build green; flash.
2. `hotWaterSet {"enable":1,"setpoint":55}` → `hotWaterStatus` shows
   `enable:true`, `setpoint:55`; thermostat `otStatus` demand shows
   `dhwEnable:true`, `dhwSetpoint:55` (the values pushed to the gateway —
   ID 0 DHW bit + ID 56).
3. `hotWaterSet {"enable":0}` → demand `dhwEnable:false`.
4. Gateway-side DHW actuation (`dhwActive`/`dhwTemp`) is not exercisable: the
   gateway emulator reports `dhwPresent:false` and no DHW temp — verified by
   review that the readout maps `OtBoilerState` fields correctly; real
   behaviour needs a boiler with DHW.
5. Persistence: set enable + setpoint, reboot, confirm both retained.
6. `otSet` no longer exists (command returns not-found); `climateSet` and
   `hotWaterSet` cover all demand.

## Out of scope

- **Legionella protection** and **DHW scheduling/comfort profiles** — see
  `docs/backlog/dhw-advanced.md` (consider before a production release; not
  Step-1 blockers).
- ID 48 dynamic DHW bounds (fixed 30–80 for now).
- On-screen UI (item 7) and web page (item 8) — commands are the interim
  surface.
