# Thermostat UI (minimal) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Bring up the ST7701 panel + GT911 touch + LVGL, and show a minimal screen: big room-temp number with − / + buttons that nudge the setpoint ±0.5 °C (screen briefly shows the setpoint, then reverts to room temp after ~4 s).

**Architecture:** Reusable `St7701Panel`/`Gt911Touch` drivers (ported verbatim from `feature/ot-thermostat-dropin`) owned by `Board`, initialized inside `Board::Init` *before* the OT link (GPIO11/12 handoff). A new `DisplayManager` owns LVGL (via `esp_lvgl_port`) + the screen, reading room temp from `RoomTemperatureManager` and driving the setpoint through new `ClimateManager` methods.

**Tech Stack:** ESP-IDF v6.0, C++17, LVGL 9 + esp_lvgl_port, esp_lcd_st7701 + esp_lcd_touch_gt911, PSRAM framebuffer, `lib/rtos` (`InitState`).

## Global Constraints

- Spec: `docs/superpowers/specs/2026-07-07-thermostat-ui-design.md` governs.
- **GPIO11/12 ordering (hard):** ST7701 3-wire-SPI init must run before the OT link claims those pins as UART. Both live in `Board::Init`: panel init goes *before* `otLink_.Init(...)` (which already `gpio_reset_pin`s 11/12). `auto_del_panel_io` stays **false** on this board (true blanks the panel — proven).
- Setpoint nudge **±0.5 °C**, clamp **[5, 30]**; revert to room temp after **4 s**; room-temp refresh **~1 s**; invalid temp → `--.-°`.
- Display is **not boot-critical**: any panel/touch init failure logs and continues headless (control + web still work).
- Mode is **not** shown/settable on screen (out of scope); backlight on at boot, no timeout.
- No automated tests; each task's cycle is a green build. Hardware verification is the last task.
- Build (PowerShell): `. "C:\Espressif\tools\Microsoft.v6.0.PowerShell_profile.ps1"; $env:PYTHONUTF8="1"; $env:PYTHONIOENCODING="utf-8"; idf.py build`. Never export.ps1. Timeout 600000 ms. A dependency change needs `idf.py reconfigure` (or delete `build/` if the component manager misbehaves).
- Reference (port, don't merge): drivers `St7701Panel.h`/`Gt911Touch.h`, board `Display.h` (holds `DIYLESS_ST7701_INIT` + config)/`Touch.h`, `DisplayManager` (LVGL bring-up), `fonts/font_temp_96.c` — all on `feature/ot-thermostat-dropin`. Retrieve verbatim with `git show feature/ot-thermostat-dropin:<path>`.

---

### Task 1: Managed components + LVGL/PSRAM config

Files: `main/idf_component.yml`, `main/CMakeLists.txt`, `sdkconfig.defaults` (create if absent).

- [ ] **Step 1: Add the display dependencies**

Append to `main/idf_component.yml` under `dependencies:`:

```yaml
  lvgl/lvgl: "^9.2"
  espressif/esp_lvgl_port: "^2"
  espressif/esp_lcd_touch_gt911: "^1"
  espressif/esp_lcd_st7701:
    version: "^2"
    rules:
      - if: "target in [esp32s3]"
  espressif/esp_lcd_panel_io_additions:
    version: "^1"
    rules:
      - if: "target in [esp32s3]"
```

- [ ] **Step 2: Add the components to main's REQUIRES**

In `main/CMakeLists.txt`, add to `COMPONENT_REQUIRES` (managed-component target names use the `owner__name` form):

```cmake
    esp_lcd
    lvgl__lvgl
    espressif__esp_lvgl_port
    espressif__esp_lcd_touch
    espressif__esp_lcd_touch_gt911
    espressif__esp_lcd_st7701
    espressif__esp_lcd_panel_io_additions
```

- [ ] **Step 3: LVGL fonts via Kconfig**

Add to `sdkconfig.defaults` (these enable the Montserrat sizes the UI uses; PSRAM is already configured for this board):

```
CONFIG_LV_FONT_MONTSERRAT_16=y
CONFIG_LV_FONT_MONTSERRAT_20=y
CONFIG_LV_FONT_MONTSERRAT_28=y
CONFIG_LV_FONT_MONTSERRAT_48=y
```

- [ ] **Step 4: Reconfigure + build**

Run: `idf.py reconfigure` then the build command. Expected: components fetch and compile; `Project build complete`. (Nothing uses them yet — this proves the deps resolve and LVGL configures.) If the component manager errors, delete `build/` and `dependencies.lock` and rebuild.

- [ ] **Step 5: Commit**

```bash
git add main/idf_component.yml main/CMakeLists.txt sdkconfig.defaults dependencies.lock
git commit -m "Add LVGL + ST7701/GT911 display components and LVGL font config"
```

---

### Task 2: Port drivers; add panel + touch + backlight to Board

Files: `main/hardware/drivers/St7701Panel.h`, `main/hardware/drivers/Gt911Touch.h`, `main/hardware/boards/diyless_thermostat_3/DisplayInit.h` (new), `Board.h`, `Board.cpp`.

**Interfaces produced:** `Board::GetPanel() -> esp_lcd_panel_handle_t`, `Board::GetTouch() -> esp_lcd_touch_handle_t`, `Board::SetBacklight(bool)`.

- [ ] **Step 1: Port the two drivers verbatim**

```bash
git show feature/ot-thermostat-dropin:main/hardware/drivers/St7701Panel.h > main/hardware/drivers/St7701Panel.h
git show feature/ot-thermostat-dropin:main/hardware/drivers/Gt911Touch.h  > main/hardware/drivers/Gt911Touch.h
```
These are header-only and board-agnostic — no edits needed. (`St7701Panel` exposes `Init(const St7701Config&)`, `panel()`, `ok()`; `Gt911Touch` exposes `Init(const Gt911Config&)`, `handle()`, `ok()`.)

- [ ] **Step 2: Extract the panel init table into a board header**

Create `main/hardware/boards/diyless_thermostat_3/DisplayInit.h` holding the `DIYLESS_ST7701_INIT` table. Source it from the demo `Display.h`:

```bash
git show feature/ot-thermostat-dropin:main/hardware/boards/diyless_thermostat_3/Display.h > /tmp/demo_display.h
```
Copy the `#pragma once`, the `esp_lcd_st7701.h` include, and the full `static const st7701_lcd_init_cmd_t DIYLESS_ST7701_INIT[] = { ... };` array (the ESPHome ST7701S_1 sequence — panel-specific, avoids vertical banding) into `DisplayInit.h`. Do **not** copy the demo's `Display`/`Backlight` wrapper class — the config-building goes into `Board.cpp` (next step) to match this repo's raw-driver-in-Board idiom.

- [ ] **Step 3: Add members + accessors to Board.h**

Add includes: `#include "drivers/St7701Panel.h"`, `#include "drivers/Gt911Touch.h"`. Add private members `St7701Panel panel_;` and `Gt911Touch touch_;`. Add public accessors:

```cpp
    esp_lcd_panel_handle_t GetPanel() { return panel_.panel(); }
    esp_lcd_touch_handle_t GetTouch() { return touch_.handle(); }
    void SetBacklight(bool on) { gpio_set_level((gpio_num_t)BoardConfig::LCD_PIN_BACKLIGHT, on ? 1 : 0); }
```

- [ ] **Step 4: Init panel + touch in Board.cpp (before the OT link)**

Add `#include "DisplayInit.h"`. In `Board::Init`, **after** the AHT20 block and **before** the `otLink_.Init(...)` block, insert:

```cpp
    // Backlight GPIO — held off until DisplayManager has a first frame up.
    gpio_config_t bk = {};
    bk.pin_bit_mask = 1ULL << BoardConfig::LCD_PIN_BACKLIGHT;
    bk.mode = GPIO_MODE_OUTPUT;
    gpio_config(&bk);
    gpio_set_level((gpio_num_t)BoardConfig::LCD_PIN_BACKLIGHT, 0);

    // ST7701 panel. MUST init before the OT link: the 3-wire SPI uses GPIO11/12,
    // which otLink_.Init() then reclaims (gpio_reset_pin) as its UART. Keep
    // auto_del_panel_io = false (true blanks this panel).
    {
        St7701Config cfg{};
        cfg.spi_cs  = (gpio_num_t)BoardConfig::LCD_SPI_CS;
        cfg.spi_sck = (gpio_num_t)BoardConfig::LCD_SPI_SCK;
        cfg.spi_sda = (gpio_num_t)BoardConfig::LCD_SPI_SDA;
        cfg.spi_mode = 0;
        cfg.de = (gpio_num_t)BoardConfig::LCD_PIN_DE;
        cfg.vsync = (gpio_num_t)BoardConfig::LCD_PIN_VSYNC;
        cfg.hsync = (gpio_num_t)BoardConfig::LCD_PIN_HSYNC;
        cfg.pclk = (gpio_num_t)BoardConfig::LCD_PIN_PCLK;
        cfg.data_pins = BoardConfig::LCD_DATA_PINS;
        cfg.h_res = BoardConfig::LCD_H_RES;
        cfg.v_res = BoardConfig::LCD_V_RES;
        cfg.pclk_hz = BoardConfig::LCD_PIXEL_CLOCK_HZ;
        cfg.hsync_pulse_width = BoardConfig::LCD_HSYNC_PULSE_WIDTH;
        cfg.hsync_back_porch = BoardConfig::LCD_HSYNC_BACK_PORCH;
        cfg.hsync_front_porch = BoardConfig::LCD_HSYNC_FRONT_PORCH;
        cfg.vsync_pulse_width = BoardConfig::LCD_VSYNC_PULSE_WIDTH;
        cfg.vsync_back_porch = BoardConfig::LCD_VSYNC_BACK_PORCH;
        cfg.vsync_front_porch = BoardConfig::LCD_VSYNC_FRONT_PORCH;
        cfg.pclk_active_neg = BoardConfig::LCD_PCLK_ACTIVE_NEG;
        cfg.pclk_idle_high = BoardConfig::LCD_PCLK_IDLE_HIGH;
        cfg.clk_src = LCD_CLK_SRC_PLL160M;   // DIYLESS pins PLL160M (anti-jitter)
        cfg.reset_gpio = (gpio_num_t)BoardConfig::LCD_PIN_RESET;
        cfg.rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB;
        cfg.init_cmds = DIYLESS_ST7701_INIT;
        cfg.init_cmds_size = sizeof(DIYLESS_ST7701_INIT) / sizeof(DIYLESS_ST7701_INIT[0]);
        cfg.mirror_by_cmd = true;
        cfg.auto_del_panel_io = false;
        if (!panel_.Init(cfg))
            ESP_LOGE(TAG, "ST7701 panel init failed (continuing headless)");
    }

    // GT911 touch on the shared I2C bus (auto-probe 0x5D/0x14).
    if (i2cBus_)
    {
        Gt911Config tcfg{};
        tcfg.bus = i2cBus_;
        tcfg.x_max = BoardConfig::LCD_H_RES;
        tcfg.y_max = BoardConfig::LCD_V_RES;
        if (!touch_.Init(tcfg))
            ESP_LOGW(TAG, "GT911 touch init failed (continuing without touch)");
    }
```

Add `#include "esp_lcd_st7701.h"` / `#include "esp_lcd_touch.h"` to Board.cpp if the handle types aren't already visible via the driver headers (they are, transitively — add only if the build complains).

- [ ] **Step 5: Build**

Run the build. Expected: `Project build complete`. (On hardware this would now light the panel white/garbage — no LVGL yet — and keep the OT link working; that's verified in the final task.)

- [ ] **Step 6: Commit**

```bash
git add main/hardware/drivers/St7701Panel.h main/hardware/drivers/Gt911Touch.h main/hardware/boards/diyless_thermostat_3/DisplayInit.h main/hardware/boards/diyless_thermostat_3/Board.h main/hardware/boards/diyless_thermostat_3/Board.cpp
git commit -m "Board: bring up ST7701 panel + GT911 touch (before OT link); backlight control"
```

---

### Task 3: ClimateManager setpoint control surface

Files: `main/Application/ClimateManager/ClimateManager.h`, `ClimateManager.cpp`.

**Interfaces produced:** `float ClimateManager::GetUserSetpoint() const;`, `void ClimateManager::NudgeSetpoint(float deltaC);`

- [ ] **Step 1: Declare the methods**

In `ClimateManager.h` public section:

```cpp
    float GetUserSetpoint() const;
    void  NudgeSetpoint(float deltaC);   // ±, clamped to [5,30], persisted
```

- [ ] **Step 2: Implement**

In `ClimateManager.cpp`:

```cpp
float ClimateManager::GetUserSetpoint() const
{
    LOCK(mutex_);
    return userSetpoint_;
}

void ClimateManager::NudgeSetpoint(float deltaC)
{
    float sp;
    {
        LOCK(mutex_);
        sp = userSetpoint_ + deltaC;
        if (sp < kSetpointMin) sp = kSetpointMin;
        if (sp > kSetpointMax) sp = kSetpointMax;
        if (fabsf(sp - userSetpoint_) < 0.001f) return;   // no change (clamped)
        userSetpoint_ = sp;
    }
    setpointSetting_.Set(sp);
    serviceProvider_.getSettingsManager().Save();
    // Takes effect on the next control cycle (ControlStep reads userSetpoint_).
}
```

- [ ] **Step 3: Build**

Run the build. Expected: `Project build complete`.

- [ ] **Step 4: Commit**

```bash
git add main/Application/ClimateManager/ClimateManager.h main/Application/ClimateManager/ClimateManager.cpp
git commit -m "ClimateManager: GetUserSetpoint + NudgeSetpoint for the on-screen UI"
```

---

### Task 4: DisplayManager + minimal screen + wiring

Files: `main/Application/DisplayManager/DisplayManager.h`, `DisplayManager.cpp`, `main/Application/DisplayManager/fonts/font_temp_96.c`, `ServiceProvider.h`, `ApplicationContext.h`, `main.cpp`, `main/CMakeLists.txt`.

**Interfaces produced:** `ServiceProvider::getDisplayManager() -> DisplayManager&`.

- [ ] **Step 1: Port the big font**

```bash
mkdir -p main/Application/DisplayManager/fonts
git show feature/ot-thermostat-dropin:main/Application/DisplayManager/fonts/font_temp_96.c > main/Application/DisplayManager/fonts/font_temp_96.c
```
Declare it where used: `extern "C" const lv_font_t font_temp_96;`

- [ ] **Step 2: Create DisplayManager.h**

```cpp
#pragma once

#include "ServiceProvider.h"
#include "InitState.h"
#include "lvgl.h"

// Owns LVGL (via esp_lvgl_port) and the minimal screen: big room-temp number
// with -/+ buttons that nudge the setpoint (ClimateManager). Consumes the
// board's panel/touch handles. Headless-safe: no-ops if the panel didn't init.
class DisplayManager
{
    static constexpr const char *TAG = "DisplayManager";
    static constexpr uint32_t kRevertMs = 4000;   // setpoint shown, then revert
    static constexpr uint32_t kRefreshMs = 1000;  // room-temp refresh cadence

public:
    explicit DisplayManager(ServiceProvider &serviceProvider);
    DisplayManager(const DisplayManager &) = delete;
    DisplayManager &operator=(const DisplayManager &) = delete;
    DisplayManager(DisplayManager &&) = delete;
    DisplayManager &operator=(DisplayManager &&) = delete;

    void Init();

private:
    bool InitLvgl();
    void BuildUi();
    void ShowRoomTemp();          // called by the refresh timer + on revert
    void OnNudge(float deltaC);   // button handler → ClimateManager + show setpoint

    static void RefreshTimerCb(lv_timer_t *t);
    static void RevertTimerCb(lv_timer_t *t);
    static void MinusCb(lv_event_t *e);
    static void PlusCb(lv_event_t *e);

    ServiceProvider &serviceProvider_;
    InitState initState_;
    lv_display_t *lvDisplay_ = nullptr;
    lv_obj_t *bigLabel_ = nullptr;    // the temperature/setpoint number
    lv_obj_t *stateLabel_ = nullptr;  // "" (temp) or "SET" (setpoint)
    lv_timer_t *revertTimer_ = nullptr;
    bool showingSetpoint_ = false;
};
```

- [ ] **Step 3: Create DisplayManager.cpp**

`InitLvgl` is the RGB path from the demo (partial draw buffer in **internal** DMA RAM, single buffer, bounce-buffer mode — the proven anti-artifact config):

```cpp
#include "DisplayManager.h"
#include "ClimateManager/ClimateManager.h"
#include "RoomTemperatureManager/RoomTemperatureManager.h"
#include "Board.h"
#include "esp_lvgl_port.h"
#include "esp_log.h"
#include <cstdio>

extern "C" const lv_font_t font_temp_96;

DisplayManager::DisplayManager(ServiceProvider &serviceProvider)
    : serviceProvider_(serviceProvider)
{
}

void DisplayManager::Init()
{
    auto init = initState_.TryBeginInit();
    if (!init) { ESP_LOGW(TAG, "Already initialized or initializing"); return; }

    if (serviceProvider_.getBoard().GetPanel() == nullptr)
    {
        ESP_LOGW(TAG, "No panel (headless) — UI disabled");
        return;
    }
    if (!InitLvgl()) return;

    // Touch (best-effort).
    if (serviceProvider_.getBoard().GetTouch())
    {
        lvgl_port_touch_cfg_t tcfg = {};
        tcfg.disp = lvDisplay_;
        tcfg.handle = serviceProvider_.getBoard().GetTouch();
        if (lvgl_port_add_touch(&tcfg) == nullptr)
            ESP_LOGW(TAG, "lvgl_port_add_touch failed — touch disabled");
    }

    BuildUi();
    serviceProvider_.getBoard().SetBacklight(true);

    init.SetReady();
    ESP_LOGI(TAG, "Initialized");
}

bool DisplayManager::InitLvgl()
{
    lvgl_port_cfg_t port_cfg = ESP_LVGL_PORT_INIT_CONFIG();
    port_cfg.task_stack = 8192;
    port_cfg.task_affinity = 1;   // pin to core 1, leave core 0 for WiFi
    if (lvgl_port_init(&port_cfg) != ESP_OK) { ESP_LOGE(TAG, "lvgl_port_init failed"); return false; }

    lvgl_port_display_cfg_t disp_cfg = {};
    disp_cfg.panel_handle = serviceProvider_.getBoard().GetPanel();
    disp_cfg.buffer_size = 480 * 20;      // 20 lines, internal DMA RAM
    disp_cfg.double_buffer = false;
    disp_cfg.hres = 480;
    disp_cfg.vres = 480;
    disp_cfg.monochrome = false;
    disp_cfg.color_format = LV_COLOR_FORMAT_RGB565;
    disp_cfg.flags.buff_dma = true;
    disp_cfg.flags.buff_spiram = false;
    disp_cfg.flags.swap_bytes = false;
    disp_cfg.flags.full_refresh = false;

    lvgl_port_display_rgb_cfg_t rgb_cfg = {};
    rgb_cfg.flags.bb_mode = true;
    rgb_cfg.flags.avoid_tearing = false;

    lvDisplay_ = lvgl_port_add_disp_rgb(&disp_cfg, &rgb_cfg);
    if (lvDisplay_ == nullptr) { ESP_LOGE(TAG, "lvgl_port_add_disp_rgb failed"); return false; }
    return true;
}

// All LVGL object access happens under lvgl_port_lock (LVGL runs in its own task).

void DisplayManager::BuildUi()
{
    if (!lvgl_port_lock(0)) return;

    lv_obj_t *scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, lv_color_black(), 0);

    stateLabel_ = lv_label_create(scr);
    lv_obj_set_style_text_color(stateLabel_, lv_color_hex(0x888888), 0);
    lv_obj_set_style_text_font(stateLabel_, &lv_font_montserrat_28, 0);
    lv_label_set_text(stateLabel_, "");
    lv_obj_align(stateLabel_, LV_ALIGN_TOP_MID, 0, 60);

    bigLabel_ = lv_label_create(scr);
    lv_obj_set_style_text_color(bigLabel_, lv_color_white(), 0);
    lv_obj_set_style_text_font(bigLabel_, &font_temp_96, 0);
    lv_obj_align(bigLabel_, LV_ALIGN_CENTER, 0, -20);

    // Two big buttons along the bottom.
    lv_obj_t *minus = lv_button_create(scr);
    lv_obj_set_size(minus, 200, 130);
    lv_obj_align(minus, LV_ALIGN_BOTTOM_LEFT, 20, -20);
    lv_obj_add_event_cb(minus, MinusCb, LV_EVENT_CLICKED, this);
    lv_obj_t *ml = lv_label_create(minus);
    lv_obj_set_style_text_font(ml, &lv_font_montserrat_48, 0);
    lv_label_set_text(ml, "-");
    lv_obj_center(ml);

    lv_obj_t *plus = lv_button_create(scr);
    lv_obj_set_size(plus, 200, 130);
    lv_obj_align(plus, LV_ALIGN_BOTTOM_RIGHT, -20, -20);
    lv_obj_add_event_cb(plus, PlusCb, LV_EVENT_CLICKED, this);
    lv_obj_t *pl = lv_label_create(plus);
    lv_obj_set_style_text_font(pl, &lv_font_montserrat_48, 0);
    lv_label_set_text(pl, "+");
    lv_obj_center(pl);

    lvgl_port_unlock();

    ShowRoomTemp();
    lv_timer_create(RefreshTimerCb, kRefreshMs, this);
}

void DisplayManager::ShowRoomTemp()
{
    float t = 0;
    bool valid = serviceProvider_.getRoomTemperatureManager().GetRoomTemperature(t);
    char buf[16];
    if (valid) snprintf(buf, sizeof(buf), "%.1f\xC2\xB0", t);   // UTF-8 degree
    else       snprintf(buf, sizeof(buf), "--.-\xC2\xB0");
    if (!lvgl_port_lock(0)) return;
    lv_label_set_text(stateLabel_, "");
    lv_label_set_text(bigLabel_, buf);
    lvgl_port_unlock();
    showingSetpoint_ = false;
}

void DisplayManager::OnNudge(float deltaC)
{
    serviceProvider_.getClimateManager().NudgeSetpoint(deltaC);
    float sp = serviceProvider_.getClimateManager().GetUserSetpoint();
    char buf[16];
    snprintf(buf, sizeof(buf), "%.1f\xC2\xB0", sp);
    if (lvgl_port_lock(0))
    {
        lv_label_set_text(stateLabel_, "SET");
        lv_label_set_text(bigLabel_, buf);
        if (revertTimer_) lv_timer_reset(revertTimer_);
        else revertTimer_ = lv_timer_create(RevertTimerCb, kRevertMs, this);
        lvgl_port_unlock();
    }
    showingSetpoint_ = true;
}

void DisplayManager::RefreshTimerCb(lv_timer_t *t)
{
    auto *self = static_cast<DisplayManager *>(lv_timer_get_user_data(t));
    if (!self->showingSetpoint_) self->ShowRoomTemp();   // don't clobber setpoint view
}

void DisplayManager::RevertTimerCb(lv_timer_t *t)
{
    auto *self = static_cast<DisplayManager *>(lv_timer_get_user_data(t));
    lv_timer_delete(t);
    self->revertTimer_ = nullptr;
    self->ShowRoomTemp();
}

void DisplayManager::MinusCb(lv_event_t *e)
{
    static_cast<DisplayManager *>(lv_event_get_user_data(e))->OnNudge(-0.5f);
}

void DisplayManager::PlusCb(lv_event_t *e)
{
    static_cast<DisplayManager *>(lv_event_get_user_data(e))->OnNudge(+0.5f);
}
```

Note: `RefreshTimerCb`/`RevertTimerCb`/button callbacks run inside the LVGL task, so they may touch LVGL objects directly; `ShowRoomTemp`/`OnNudge` still take `lvgl_port_lock` because they are also reachable outside that task path and locking is re-entrant-safe here. `NudgeSetpoint`/`GetRoomTemperature` are mutex-guarded in their managers.

- [ ] **Step 4: Wire it in**

- `ServiceProvider.h`: add `class DisplayManager;` and `virtual DisplayManager& getDisplayManager() = 0;`.
- `ApplicationContext.h`: include `DisplayManager/DisplayManager.h`; add accessor; add member `DisplayManager m_displayManager{*this};` after `m_hotWaterManager`.
- `main.cpp`: add `g_appContext.getDisplayManager().Init();` after `getHotWaterManager().Init();`, before `getUpdateManager().Init();`.
- `main/CMakeLists.txt`: add `"Application/DisplayManager/DisplayManager.cpp"` and `"Application/DisplayManager/fonts/font_temp_96.c"` to `SOURCE_FILES_LIST`, and `"Application/DisplayManager"` to `INCLUDE_DIRS_LIST`.

- [ ] **Step 5: Build**

Run the build. Expected: `Project build complete`.

- [ ] **Step 6: Commit**

```bash
git add main/Application/DisplayManager/ main/Application/ServiceProvider.h main/Application/ApplicationContext.h main/main.cpp main/CMakeLists.txt
git commit -m "Add DisplayManager: minimal LVGL screen (room temp + setpoint buttons)"
```

---

### Task 5: Hardware verification

Files: none (bench).

- [ ] **Step 1: Flash**

`. "C:\Espressif\tools\Microsoft.v6.0.PowerShell_profile.ps1"; $env:PYTHONUTF8="1"; $env:PYTHONIOENCODING="utf-8"; idf.py -p COM13 flash`

- [ ] **Step 2: Panel lights, shows temp**

Screen shows a room-temperature number (bench ~22–25 °C) on a black background with − / + buttons. Boot log: `DisplayManager: Initialized`. If the panel is dark/garbled, check the bring-up checklist (rgb_ele_order, invert, PLL160M) — but the ported DIYLESS init is proven.

- [ ] **Step 3: Buttons + revert**

Tap **+**: label switches to `SET` and increments 0.5 °C; over-the-air check `climateStatus` `userSetpoint` reflects it. After ~4 s the number reverts to room temp. Tap **−**: decrements; clamps at 5 and 30 across repeated presses.

- [ ] **Step 4: OT link still up**

`otStatus` → `linked:true` (proves the panel-init / STM32-UART GPIO11/12 ordering held). Room temp on screen tracks the AHT20.

- [ ] **Step 5: Persistence**

Set a setpoint from the screen, reboot, confirm `climateStatus` `userSetpoint` retained (same NVS path as `climateSet`).
