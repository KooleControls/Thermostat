# Room temperature manager — Design

**Date:** 2026-07-07
**Status:** Approved
**Backlog item:** `docs/backlog/room-temperature.md` (Step 1, item 4)

## Goal

One place owns "the measured room temperature": sampled on a fixed cadence,
cached with a validity signal, consumed by OpenThermManager now (ID 24) and
by ClimateManager/UI later. A dead sensor degrades to a visible, logged
fault instead of a stale value.

## Decisions

- **`RoomTemperatureManager`** — new manager, standard Strux pattern
  (`ServiceProvider&` ctor, copy/move deleted, `InitState`-guarded `Init()`).
  Init order: after Board (needs the sensor), before OpenThermManager
  (its first cycles may pull).
- **Contract:** `bool GetRoomTemperature(float &celsius)` — `false` = no
  valid recent measurement; that is the fault signal (mirrors the
  `TemperatureSensor` role-interface convention). Consumers never touch the
  sensor directly again; a future external/BLE source (Step 2) swaps in
  behind this one method.
- **Sampling:** own task (`roomtemp`, priority 5, 4096 stack), reads the
  board's `TemperatureSensor` every **5 s**, stores `{value, timestamp}`
  under the manager's mutex.
- **Validity:** the cached value is valid while the last successful read is
  **≤ 30 s** old (= 6 consecutive misses at the 5 s cadence). Timestamps
  via `esp_timer_get_time()`. Valid↔invalid transitions are logged once
  (edge-detected), matching OpenThermManager's "OT link up/down" style.
- **Bench command:** `roomTemp` → `{"valid":bool,"temp":float,"ageMs":u32}`
  registered on CommandManager (WebSocket + `POST /api/command`).
  `temp` is the last known value (0 if never read); `ageMs` the time since
  the last successful read (0 if never read).
- **OpenThermManager consumes it:** `GetWriteValue(ID_TROOM)` calls
  `getRoomTemperatureManager().GetRoomTemperature()` instead of the board
  sensor; the "temporary feed" comment goes away. Invalid → return false →
  the rotation slot is skipped, exactly today's behavior.
- **Safe state is item 5's job:** "sensor fault → drop heat demand" belongs
  to ClimateManager (it owns demand). This item delivers the validity
  signal it will act on. Until then, a faulted sensor merely stops ID 24
  updates (gateway keeps its last value).

## Files

| File | Content |
|---|---|
| `main/Application/RoomTemperatureManager/RoomTemperatureManager.h/.cpp` | Manager, task loop, `GetRoomTemperature`, `roomTemp` command. |
| `main/Application/ServiceProvider.h` | `getRoomTemperatureManager()` accessor. |
| `main/Application/ApplicationContext.h` | Member + accessor. |
| `main/main.cpp` | `Init()` after Board, before OpenTherm. |
| `main/CMakeLists.txt` | Source + include dir. |
| `main/Application/OpenThermManager/OpenThermManager.cpp` | `GetWriteValue(ID_TROOM)` pulls from the manager. |
| `docs/backlog/room-temp-calibration.md` | New deferred item (see below). |
| `docs/backlog/room-temperature.md` | Trim scope: calibration/humidity moved out. |

## Out of scope

- **Calibration (offset or curve)** — split into its own backlog item
  `room-temp-calibration.md`: the T3 self-heats ≈ +7 °C but the error
  likely varies with display load, so a constant offset may not suffice;
  needs its own brainstorm (offset vs curve vs reference-calibration),
  plus vendor input (RA2-389).
- **Humidity** — nothing consumes it (OT carries none in the thermostat
  path; ID 78 is ventilation-only). Joins with `thermostat-ui` (item 7).
- **External/BLE sensor sources** — Step 2; the single-method contract is
  the seam they slot into.
- **Smoothing/filtering** — the DIYLESS reference PID takes the raw sensor;
  revisit in `climate-pid` if the trace demands it.

## Verification

1. Build green; flash.
2. `roomTemp` command returns `valid:true` and a plausible temperature
   (bench ~20–30 °C), `ageMs` < 5000 on repeat calls.
3. Gateway still sees live room temp: KC1 `DTMP ACT` matches `roomTemp`
   within a rotation period.
4. Fault path: not exercisable on hardware (AHT20 soldered on-board) —
   verified by review of the 30 s staleness logic and the skip-slot
   behavior it triggers in OpenThermManager. **Amendment (final review):**
   the pre-existing `Aht20Sensor` driver could serve a frozen value forever
   after a post-boot I2C failure (failed trigger halted measurement;
   `have_` never expired), making the staleness window unreachable for that
   mode — fixed on this branch (re-trigger recovery + 3-consecutive-failure
   expiry), so a dead bus now surfaces as `ReadTemperature() == false`
   within 3 poll cycles and the manager faults ~30 s later.
