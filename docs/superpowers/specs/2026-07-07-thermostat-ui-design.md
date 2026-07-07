# Thermostat on-screen UI (minimal) — Design

**Date:** 2026-07-07
**Status:** Approved
**Backlog item:** `docs/backlog/thermostat-ui.md` (Step 1, item 7) — **deliberately scoped down** from the full home screen to a first-light minimal UI.

## Goal

Get pixels on the DIYLESS T3's 480×480 ST7701 panel and put the simplest
useful thermostat screen on it: the room temperature, big and centred, with
two large touch buttons that nudge the setpoint ±0.5 °C. After a press the
number briefly shows the setpoint, then reverts to room temp. That's the
whole UI. The bulk of the work is bringing up the display + touch + LVGL
stack, which has no prior existence on the Strux baseline.

## Reference material (mine, don't merge)

The parked `feature/ot-thermostat-dropin` branch has **proven drivers for this
exact panel** — reuse them:

- `main/hardware/drivers/St7701Panel.h` — ST7701 3-wire-SPI-init + RGB panel wrapper.
- `main/hardware/drivers/Gt911Touch.h` — GT911 over the shared I2C bus.
- `main/hardware/boards/diyless_thermostat_3/Display.h` — the **panel-specific
  `DIYLESS_ST7701_INIT` sequence** (transcribed from the ESPHome ST7701S_1
  sequence; the generic esp_lcd default produces vertical banding — this exact
  sequence is what locks the panel).
- `main/hardware/boards/diyless_thermostat_3/Touch.h` — GT911 wiring (addr auto-probe 0x5D/0x14).
- `main/Application/DisplayManager/` — LVGL/esp_lvgl_port setup to mine; its
  *screens* are the old complex layout — write a fresh minimal screen instead.
- `main/Application/DisplayManager/fonts/font_temp_96.c` — a 96 px numeric font
  for the big temperature (reuse; nicer than the ≤48 px built-ins).

## Decisions

### Layering (follows the repo's Board-owns-drivers pattern)

- **Board owns the panel + touch driver instances**, mirroring how it already
  owns `Aht20Sensor` and `Stm32OpenThermLink`. `Board` gains an `St7701Panel`
  and a `Gt911Touch` (ported from the demo), plus accessors:
  `esp_lcd_panel_handle_t GetPanel()`, `Gt911Touch& GetTouch()` (or its
  `esp_lcd_touch_handle_t`), and `void SetBacklight(bool)`.
- **`DisplayManager` (new Application manager) owns LVGL + the screen only.**
  It consumes the board's panel/touch handles, `ClimateManager`, and
  `RoomTemperatureManager`. No hardware-register or pin code lives here.

### The GPIO11/12 ordering (the one hard constraint)

GPIO11/12 are shared: the ST7701 3-wire-SPI init lines **and** the STM32 OT
UART. The panel's SPI init must run **before** the OT link claims them. This
is handled entirely inside `Board::Init`, in this order:

1. I2C bus (already there) → 2. AHT20 (already there) →
3. **panel init** (ST7701 SPI-init on GPIO11/12/1, then RGB) →
4. **touch init** (GT911 on I2C) →
5. STM32 OT link (`otLink_.Init` already `gpio_reset_pin`s 11/12 before
   configuring the UART — so it reclaims them cleanly after the SPI init).

Because the pin handoff is contained in `Board::Init`, **`DisplayManager` has
no init-order constraint** — it runs in its natural slot (after the managers
it reads). Init order: Board → RoomTemperature → OpenTherm → Climate →
HotWater → **Display** → Update → WebServer.

### The screen

- 480×480. A big centred numeric label (font_temp_96) showing a temperature,
  and two large touch buttons: **−** (left) and **+** (right), along the
  bottom. A small label distinguishes the two display states (e.g. shows
  `SET` while showing the setpoint).
- **Default state:** room temperature, refreshed by a ~1 s LVGL timer. Invalid
  reading (`RoomTemperatureManager::GetRoomTemperature` returns false) →
  `--.-°`.
- **On − / +:** call `ClimateManager::NudgeSetpoint(±0.5)`, switch the label to
  the setpoint (`SET 21.5°`), and start/restart a **4 s** revert timer. Each
  further press re-nudges and resets the timer. On timeout, revert to room
  temp.
- **Backlight:** on at boot (`SetBacklight(true)`). No screen timeout.

### ClimateManager gains a direct control surface

So the UI drives control through method calls, not the JSON command layer, add
two public methods to `ClimateManager` (reusing the existing clamp/persist/push
logic from `Cmd_ClimateSet`):

- `float GetUserSetpoint() const;`
- `void NudgeSetpoint(float deltaC);` — applies `delta`, clamps to [5, 30],
  persists (`SettingsManager::Save()`), and takes effect on the next control
  cycle. Thread-safe (takes the manager's mutex); safe to call from the LVGL
  task.

### Dependencies (managed components)

Add to `main/idf_component.yml` (this board only — not the demo's full
multi-board superset):

- `lvgl/lvgl: "^9.2"`
- `espressif/esp_lvgl_port: "^2"`
- `espressif/esp_lcd_touch_gt911: "^1"`
- `espressif/esp_lcd_st7701: "^2"` (target-gated esp32s3)
- `espressif/esp_lcd_panel_io_additions: "^1"` (target-gated esp32s3)

LVGL needs an `lv_conf.h` / Kconfig config — mine the demo's (enable the fonts
used, RGB565, the tick source esp_lvgl_port expects). Framebuffer(s) in PSRAM
(8 MB octal available); use the double-buffer/bounce config the demo's
`St7701Panel` + esp_lvgl_port setup proved on this panel.

## Error handling

- Panel or touch init failure in `Board::Init` → log an error and continue
  **headless**; the thermostat still runs (control, web UI, OT). The screen is
  not boot-critical (same policy as the AHT20/I2C failure path already there).
- `DisplayManager::Init` guards on a valid panel handle; if the board came up
  headless, it logs and no-ops rather than starting LVGL.

## Files

| File | Content |
|---|---|
| `main/hardware/drivers/St7701Panel.h` | Ported ST7701 3-wire-SPI-init + RGB panel. |
| `main/hardware/drivers/Gt911Touch.h` | Ported GT911 touch driver. |
| `main/hardware/boards/diyless_thermostat_3/Board.h/.cpp` | Own panel + touch; init in the order above; `GetPanel()`/`GetTouch()`/`SetBacklight()`. |
| `main/Application/DisplayManager/DisplayManager.h/.cpp` | LVGL + esp_lvgl_port bring-up, the minimal screen, refresh + revert timers. |
| `main/Application/DisplayManager/fonts/font_temp_96.c` (+ header) | Big numeric font (reused). |
| `main/Application/ClimateManager/ClimateManager.h/.cpp` | Add `GetUserSetpoint()` + `NudgeSetpoint()`. |
| `main/Application/ServiceProvider.h` | `getDisplayManager()` accessor + forward decl. |
| `main/Application/ApplicationContext.h` | Member (after HotWater) + accessor. |
| `main/main.cpp` | `Init()` after HotWater, before Update. |
| `main/CMakeLists.txt` | DisplayManager source + include dir; font source. |
| `main/idf_component.yml` | LVGL + esp_lvgl_port + ST7701 + GT911 + panel_io_additions. |
| `lv_conf.h` / sdkconfig | LVGL config (mined). |

## Verification (hardware)

1. Build green; flash. Boot log shows panel + touch init OK (or a clean
   headless fallback if a cable's off).
2. Screen shows live room temperature, updating within a couple of seconds of
   an AHT20 change (warm the sensor).
3. Tap **+**: label switches to `SET`, increments 0.5 °C; `climateStatus`
   `userSetpoint` reflects it; reverts to room temp after ~4 s.
4. Tap **−**: decrements 0.5 °C; clamps at 5 and 30 across repeated presses.
5. **OT link still up** (`otStatus` `linked:true`) — proves the panel-init /
   STM32-UART GPIO11/12 ordering is correct.
6. Setpoint set from the screen persists across reboot (it goes through the
   same NVS path as `climateSet`).

## Out of scope (deliberately minimal)

- Mode (Heat/Cool/Off) is **not settable or shown** on the screen — web/command
  only. Consequence: on-device you can change the target temperature but not
  turn the system on/off. Acceptable for first-light; revisit before shipping.
- No DHW controls, activity/flame icons, fault indication, OT-link indicator,
  or setpoint arc (the full backlog home screen — a later item).
- No screen timeout, backlight dimming/PWM, or knob input.
- Colour theme/polish beyond legibility.
