# OpenTherm link + master manager — Design

**Date:** 2026-07-06
**Status:** Approved
**Backlog items:** `docs/backlog/opentherm-link.md` + `docs/backlog/opentherm-master-manager.md` (Step 1, items 2+3, done together — the driver has no consumer without the manager, the manager can't be validated without the driver)

**Header sketches (part of this spec):** `docs/sketches/opentherm/OtLink.h`
and `docs/sketches/opentherm/OpenThermManager.h` — the interface, structs,
data-ID schedule, and command surface live there in compilable-looking form;
this document carries the decisions and the verification story.

## Goal

The thermostat talks OpenTherm as **master** to the gateway (OT slave /
boiler emulator): live room temp + setpoint + CH/DHW/cooling demand on the
wire, full boiler status/diagnostics read back, remote setpoint override
(ID 9) honored, link supervised. Bench-controllable without ClimateManager.

## Decisions

- **`OtLink` role interface** (3 methods: `Ready`/`Transaction`/`Recover`,
  see sketch). The ported `Stm32OpenThermLink` implements it. This dissolves
  the demo's `#ifdef BOARD_DIYLESS_THERMOSTAT_3` coupling — OpenThermManager
  is plain application code.
- **The esp32_devkit board is dropped (amendment, Bas).** This product *is*
  the DIYLESS T3; no other board means **no mocks at all** (`MockOtLink`
  never gets written; the orphaned Led example — `interfaces/Led.h`,
  `GpioLed.h`, `MockLed.h` — goes with the devkit folder).
  `diyless_thermostat_3` becomes the default `BOARD`, so the single build
  dir `build/` (esp32s3) replaces the `-B build_diyless` /
  `-DSDKCONFIG=sdkconfig_diyless` dance; CLAUDE.md build docs shrink
  accordingly. Future strux merges show devkit files as delete-conflicts —
  resolve keep-deleted, same policy as the pruned template backlog.
- **Driver is a port, not a rewrite.** `Stm32OpenThermLink` (334 lines,
  clean-room nibble protocol, proven in RA2-398) moves from the old branch
  into `hardware/drivers/`, gaining only `: public OtLink` + the interface
  methods mapped onto its existing calls. Pins from `BoardConfig` (TX12/RX11,
  BOOT0 44, NRST 13 — the empirically-verified reversal note stays).
- **Board owns the link lifecycle.** Diyless `Board::Init()` resets the STM32
  and does the CpuStatus handshake (bounded, ~1–2 s boot cost accepted);
  `Board` exposes `OtLink& GetOtLink()`. Devkit exposes the same accessor
  bound to `MockOtLink` (drivers/MockOtLink.h).
- **Push demand in, pull state out.** Whoever owns control logic pushes via
  `SetDemand()` (bench command today, ClimateManager after item 5 — the
  dependency points logic→transport). Consumers pull `GetState()` snapshots;
  OpenThermManager is the single owner of boiler-side truth and the only
  code touching the wire.
- **Temporary demand feeds** until item 5: room temp (ID 24) read directly
  from `GetTemperatureSensor()` each cycle (uncalibrated — offset arrives
  with `room-temperature`/ClimateManager, at which point room temp becomes
  part of the pushed demand so the gateway sees exactly what the PID uses);
  setpoint/enables via the `otSet`/`otStatus` command table (web-UI console).
- **Remote override (ID 9):** polled ~1 s; nonzero and different from the
  current setpoint → adopt into demand (logged); ID 16 echoes it next cycle
  (that echo is what the gateway's override logic waits for). A later local
  change simply overwrites — no special-case state.
- **Unknown-DataID handling:** a valid UNKNOWN-DATAID reply marks that ID
  unsupported → drop to a rare retry (the gateway's boiler emulator won't
  implement everything; no log spam, no wasted cycles).
- **Link supervision:** 6 consecutive `Transaction` failures (~3 s at the
  500 ms cycle) → `linked=false` (demand stops flowing — the safe state falls
  out naturally), then `Recover()` retried with backoff (5 s doubling to a
  30 s cap); transitions logged once, visible in `otStatus`. Recover() (STM32 reset + handshake) applies when the co-processor itself stops answering; bus-level failures with a healthy co-processor only mark the link down — resetting the STM32 cannot fix an unwired bus.

## Files

| File | Content |
|---|---|
| `main/hardware/interfaces/OtLink.h` | Role interface (per sketch). |
| `main/hardware/drivers/Stm32OpenThermLink.h` | Ported from `feature/ot-thermostat-dropin`, implements `OtLink`. |
| `main/hardware/boards/diyless_thermostat_3/Board.h/.cpp` | Own `Stm32OpenThermLink`; STM32 reset + handshake in `Init()`; `GetOtLink()`. |
| **Deleted** | `main/hardware/boards/esp32_devkit/` (whole folder), `interfaces/Led.h`, `drivers/GpioLed.h`, `drivers/MockLed.h`; default `BOARD` → `diyless_thermostat_3` (root + main CMakeLists); CLAUDE.md dual-variant build section replaced by the single-target flow; `docs/sketches/opentherm/` removed once the real headers land. |
| `main/Application/OpenThermManager/OpenThermManager.h/.cpp` | Manager per sketch: 500 ms master loop, demand/state structs, commands. |
| `main/Application/OpenThermManager/OtFrame.h` | 32-bit frame build/parse: parity, msg-type, data-ID, f8.8 (~40 lines). |
| Wiring | `ServiceProvider.h`, `ApplicationContext.h`, `main.cpp` (Init after Board), `main/CMakeLists.txt`. |

## Poll schedule (normative)

Every 500 ms cycle: **Status (ID 0)** with master enable bits — the <1 s
keepalive — plus one secondary message from:

- **Writes** (on change, else periodic refresh ~10 s): t_set (1, clamped to
  ID 49 bounds), room setpoint (16), room temp (24), DHW setpoint (56).
- **Reads** (rotation, each every few seconds): modulation 17, boiler temp
  25, DHW temp 26, return 28, pressure 18, outside 27, OEM fault 5, OEM
  diag 115, **max-t_set bounds ID 49 (s8/s8 — NOT 57, which is f8.8 MaxTSet;
  corrected after review)**, slave config 3.
- **Override read** (ID 9): every ~1 s.

**Reply validation (added after review — Critical):** every received frame
must match `OtFrame::Id(reply) == requested id` before use, and writes must
see `WriteAck`; the STM32 status byte from `Transact` must be checked. The
STM32 can deliver late replies from timed-out requests — without the ID
check, a stale boiler-temp reply can be adopted as an ID 9 override
setpoint.

**`SetDemand()` contract (added after review):** dirty-marking is
change-detected (compare against current demand) so a periodic caller (the
future PID, ~1 s) cannot pin the rotation at slot 0 and starve the back
half of the schedule.

## Verification

1. The (single) diyless build green: `idf.py set-target esp32s3 && idf.py build`
   in the default `build/` dir.
2. Hardware boot: STM32 `cpuVer/fwVer/boardRev` handshake line in the log.
3. Against the gateway (THR=1, `ram` build, observed over KC1/TCP): gateway
   shows live room temp (ID 24) and setpoint (ID 16); `otSet ch=1
   setpoint=<above room temp>` → gateway `HeatingActive`; `otStatus` shows
   boiler state fields populating and `linked=true`.
4. Override: push a setpoint from the gateway side (reservation/KC1) →
   thermostat log shows adoption, `otStatus` shows the new setpoint, gateway
   sees it echoed in ID 16 within its override timeout.
5. DHW + cooling bits visible to the gateway as far as its emulator
   supports them (cooling needs gateway `smarthome.coolingSupportEnabled`).
6. Link supervision: disconnect/reconnect the OT wire → `linked` drops and
   recovers; unplugging only the boiler side must not crash the loop.

## Out of scope

PID/mode logic and setpoint persistence (item 5 `climate-pid`), calibrated
room temp (item 4 `room-temperature`), on-screen UI (item 7), web page
(item 8 — the console commands are the interim surface), KC extensions over
OT (Step 2, RA2-396).
