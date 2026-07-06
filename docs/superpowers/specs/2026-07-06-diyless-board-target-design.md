# DIYLESS Thermostat 3 board target — Design

**Date:** 2026-07-06
**Status:** Approved
**Backlog item:** `docs/backlog/diyless-board-target.md` (Step 1, item 1)

## Goal

Add the DIYLESS OpenTherm Thermostat 3 (hw rev v3.3) as a board target so the
firmware builds for and boots on the real product hardware: ESP32-S3, 8 MB
flash, 8 MB octal PSRAM, WiFi + web UI up, console on USB-Serial/JTAG, AHT20
readable. **Minimal boot scope** — no display, no touch, no OpenTherm link;
those drivers arrive with their own feature items (`thermostat-ui`,
`opentherm-link`).

## Decisions

- **New-pattern port, not a copy.** The old board folder (branch
  `feature/ot-thermostat-dropin`) is proven but header-only-HAL shaped. This
  board follows the current Strux layering: a `Board` class owns buses and
  driver instances and exposes the duck-typed capability surface.
  Rejected: verbatim copy of the old HAL (forks the application surface per
  board); bare boot without AHT20 (false economy — it's the next item's input).
- **No LED anywhere on this board.** The T3 has no user LED, and the Led role
  in Strux is example code — the DIYLESS `Board` exposes **no** `GetLed()` and
  binds no mock. Duck typing makes this free: nothing in the application
  consumes `Led`. The `esp32_devkit` example board keeps its LED untouched
  (template heritage; minimizes future `strux/main` merge friction).
- **`BoardConfig.h` carries the full authoritative pin map verbatim** —
  including the LCD panel/timing constants and the STM32 OT-link section with
  the empirically-verified TX12/RX11 note — even though this item consumes
  only the I2C pins. Later items depend on that documentation; it must not be
  re-derived.
- **CI untouched.** Building the diyless variant in CI belongs to the
  `release-workflow` backlog item.

## Files

Create in `main/hardware/boards/diyless_thermostat_3/`:

| File | Content |
|---|---|
| `BoardConfig.h` | Pin map ported verbatim from the old branch (I2C SDA17/SCL18, GT911 INT 10, full ST7701 pin/timing set, backlight 46, STM32 link TX12/RX11 + BOOT0 44 / NRST 13 notes). Drop the old `LED_PIN = -1` entries. |
| `Board.h` / `Board.cpp` | New-pattern `Board`: owns the `i2c_master` bus handle (buses first — replaces the old lazy `BoardI2cBus()` singleton) and an `Aht20Sensor` instance. Surface: `GetTemperatureSensor()` / `GetHumiditySensor()` returning role-interface references (see amendment). `Init()` creates the bus, probes the AHT20, logs one temperature reading. |
| `board.cmake` | Appends `Board.cpp` to `BOARD_SOURCES`; header comment describes the hardware. |
| `sdkconfig.defaults` | Per-board overlay (root CMake already composes it): 8 MB flash, `CONFIG_PARTITION_TABLE_CUSTOM_FILENAME="partitions_8mb.csv"`, octal PSRAM, 240 MHz, **`CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG=y`** (UART0's default pins are the LCD-reset / STM32-boot lines — a UART0 console corrupts them). |

Create at repo root / drivers:

| File | Content |
|---|---|
| `partitions_8mb.csv` | Ported from the old branch — 8 MB layout: two 3 MB OTA app slots + 1.875 MB `www`, ending exactly at 8 MB. (The shared 4 MB table would *fit* but waste half the flash and cap the app at 1.5 MB — too tight once LVGL + OT land.) |
| `main/hardware/drivers/Aht20Sensor.h` | Ported chip driver, board-independent, takes the bus handle as a parameter. |

Modify:

- `CLAUDE.md` (repo): build docs — the two variants need separate build dirs
  and targets: `idf.py -B build_diyless -DBOARD=diyless_thermostat_3 set-target esp32s3`
  then `idf.py -B build_diyless -DBOARD=diyless_thermostat_3 build`.
  (Plain `idf.py build` keeps building the esp32 devkit variant in `build/`.)

## Error handling

- AHT20 probe/read failure: log an error, keep booting — the sensor is not
  boot-critical. (`room-temperature` later defines demand-side behavior.)
- Unknown `BOARD` value already fails the build with an explicit CMake error
  (existing mechanism, nothing to add).

## Verification

1. **No regression:** `idf.py build` (devkit, esp32) still green.
2. **New variant builds:** `idf.py -B build_diyless -DBOARD=diyless_thermostat_3 set-target esp32s3 && idf.py -B build_diyless -DBOARD=diyless_thermostat_3 build` green; partition table fits 8 MB.
3. **On hardware (DIYLESS T3):** flash `build_diyless`; USB-Serial/JTAG console
   shows all managers Init; one AHT20 temperature line logged; AP fallback or
   configured WiFi comes up; web UI reachable and login works. Requires the
   physical board on a COM port.

## Amendment (2026-07-06, during implementation — Bas)

Role interfaces are defined **up front** (project preference: they're cheap;
deviates from Strux's wait-for-first-consumer rule): minimal
`interfaces/TemperatureSensor.h` and `interfaces/HumiditySensor.h` (one Read
method each; the bool return is the failure signal — no `ok()`), implemented
by `Aht20Sensor`. Split roles instead of one AmbientSensor so capability is
the Board's compile-time surface (no `HasHumidity()` probe). `DefaultOffsetC()`
dropped: calibration offset is application configuration (`room-temperature`
item), not a driver property.

## Out of scope

Display/touch bring-up (→ `thermostat-ui`), STM32 OT link (→
`opentherm-link`), ambient-sensor role interface (→ `room-temperature`),
CI matrix (→ `release-workflow`), branding (→ `branding`).
