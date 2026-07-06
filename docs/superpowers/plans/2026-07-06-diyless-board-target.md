# DIYLESS Thermostat 3 Board Target Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add the DIYLESS OpenTherm Thermostat 3 (ESP32-S3) as a board target so the firmware builds for and boots on the real product hardware — WiFi + web UI up, USB-Serial/JTAG console, AHT20 readable. No display/touch/OT drivers (later backlog items).

**Architecture:** New-pattern board port: `main/hardware/boards/diyless_thermostat_3/` gets a `Board` class that owns the shared I2C master bus and an `Aht20Sensor` instance (surface: `GetAmbientSensor()`, **no** `GetLed()` — nothing consumes `Led`, and the T3 has no LED). `BoardConfig.h` carries the full authoritative pin map verbatim from the proven old branch, including LCD + STM32 sections used by later items. A per-board `sdkconfig.defaults` (composed automatically by the root CMakeLists) switches to 8 MB flash + custom partition table, octal PSRAM, 240 MHz, and USB-Serial/JTAG console.

**Tech Stack:** ESP-IDF v6.0 (EIM install on this machine), CMake board fragments, `i2c_master` driver.

## Global Constraints

- Repo: `c:\Workspace\KC1245 Gateway workspace\Thermostat`, branch off `main` → `feature/diyless-board-target`. Push to origin (KooleControls/Thermostat).
- Spec: `docs/superpowers/specs/2026-07-06-diyless-board-target-design.md`.
- Source of ported files is branch `feature/ot-thermostat-dropin` — port via `git show`, do not retype.
- ESP-IDF activation (PowerShell): `$env:PYTHONUTF8="1"; $env:PYTHONIOENCODING="utf-8"; . "C:\Espressif\tools\Microsoft.v6.0.PowerShell_profile.ps1"`. Never use `export.ps1`.
- Two build dirs: devkit stays in `build/` (esp32); diyless uses `build_diyless/` (esp32s3). Never run `set-target` against the wrong dir.
- No automated test suite in this repo — verification is building both variants (and flashing hardware in Task 4).
- Commit messages end with: `Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>`

---

### Task 1: Branch + port the shared driver and partition table

**Files:**
- Create: `main/hardware/drivers/Aht20Sensor.h` (verbatim port)
- Create: `partitions_8mb.csv` (verbatim port)

**Interfaces:**
- Produces: class `Aht20Sensor` with `bool Init(i2c_master_bus_handle_t bus)`, `bool ReadTemperature(float &celsius)`, `bool ReadHumidity(float &percent)`, `bool HasHumidity() const`, `float DefaultOffsetC() const`, `bool ok() const` — consumed by Task 2's `Board`.
- Produces: `partitions_8mb.csv` — referenced by Task 2's `sdkconfig.defaults`.

- [ ] **Step 1: Create the branch**

```bash
cd "/c/Workspace/KC1245 Gateway workspace/Thermostat"
git checkout -b feature/diyless-board-target main
```

Expected: `Switched to a new branch 'feature/diyless-board-target'`.

- [ ] **Step 2: Port both files verbatim from the old branch**

```bash
git show feature/ot-thermostat-dropin:main/hardware/drivers/Aht20Sensor.h > main/hardware/drivers/Aht20Sensor.h
git show feature/ot-thermostat-dropin:partitions_8mb.csv > partitions_8mb.csv
```

Expected: both files exist; `Aht20Sensor.h` is 134 lines; `partitions_8mb.csv` has ota_0/ota_1 at 0x300000 each and www 0x1E0000 ending at 0x800000.

- [ ] **Step 3: Commit**

```bash
git add main/hardware/drivers/Aht20Sensor.h partitions_8mb.csv
git commit -m "Port AHT20 driver + 8MB partition table from ot-thermostat-dropin

Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>"
```

### Task 2: The diyless_thermostat_3 board folder

**Files:**
- Create: `main/hardware/boards/diyless_thermostat_3/BoardConfig.h`
- Create: `main/hardware/boards/diyless_thermostat_3/Board.h`
- Create: `main/hardware/boards/diyless_thermostat_3/Board.cpp`
- Create: `main/hardware/boards/diyless_thermostat_3/board.cmake`
- Create: `main/hardware/boards/diyless_thermostat_3/sdkconfig.defaults`

**Interfaces:**
- Consumes: `Aht20Sensor` from Task 1; `InitState` (`lib/rtos/InitState.h`); `ServiceProvider`.
- Produces: class `Board` with `explicit Board(ServiceProvider&)`, `void Init()`, `Aht20Sensor& GetAmbientSensor()` — the duck-typed surface `ApplicationContext`/`main.cpp` already compile against (`getBoard().Init()`); note NO `GetLed()`.

- [ ] **Step 1: Port BoardConfig.h and strip the LED block**

```bash
git show feature/ot-thermostat-dropin:main/hardware/boards/diyless_thermostat_3/BoardConfig.h > main/hardware/boards/diyless_thermostat_3/BoardConfig.h
```

Then apply two edits with the Edit tool:

Edit A — delete the LED constants (the T3 has no user LED and this product exposes no Led role):

```cpp
// DELETE these three lines:
    // No separate user LED on this board.
    static constexpr int LED_PIN = -1;
    static constexpr bool LED_ACTIVE_HIGH = true;
```

Edit B — the header comment's last paragraph says the STM32 link is "out of scope" for the old dumb-display firmware; replace that sentence so it doesn't mislead:

```cpp
// OLD (end of the NOTE paragraph):
// This (dumb-display) firmware drives only the panel/touch/sensor; the STM32
// link is out of scope here.
// NEW:
// The STM32 link constants below are consumed by the opentherm-link feature;
// the LCD/touch constants by thermostat-ui. This board file is the single
// authoritative pin map for all of them.
```

- [ ] **Step 2: Write Board.h**

```cpp
#pragma once

#include "ServiceProvider.h"
#include "InitState.h"
#include "BoardConfig.h"
#include "drivers/Aht20Sensor.h"
#include "driver/i2c_master.h"

// ──────────────────────────────────────────────────────────────
// Board for the DIYLESS OpenTherm Thermostat 3 (hw rev v3.3).
// ESP32-S3 · 8 MB flash · 8 MB octal PSRAM · 4.0" 480x480 ST7701S ·
// GT911 touch · AHT20 · STM32L051 OpenTherm co-processor.
//
// Owns the shared I2C master bus (GT911 + AHT20 sit on SDA17/SCL18) and
// every driver instance. Exposes the duck-typed capability surface the
// application compiles against. This board has no user LED and exposes
// no Led role. Display/touch/OT-link drivers arrive with their feature
// items (thermostat-ui, opentherm-link) and will be owned here too.
// ──────────────────────────────────────────────────────────────

class Board
{
    static constexpr const char *TAG = "Board";

public:
    explicit Board(ServiceProvider &serviceProvider);

    Board(const Board &) = delete;
    Board &operator=(const Board &) = delete;
    Board(Board &&) = delete;
    Board &operator=(Board &&) = delete;

    void Init();

    // Concrete escape-hatch accessor; the ambient-sensor role interface
    // is defined by the room-temperature feature item.
    Aht20Sensor &GetAmbientSensor() { return ambientSensor_; }

private:
    ServiceProvider &serviceProvider_;
    InitState initState_;

    // Hardware instances — buses first, then the drivers that use them.
    i2c_master_bus_handle_t i2cBus_ = nullptr;
    Aht20Sensor ambientSensor_;
};
```

- [ ] **Step 3: Write Board.cpp**

```cpp
#include "Board.h"
#include "esp_log.h"

Board::Board(ServiceProvider &ctx)
    : serviceProvider_(ctx)
{
}

void Board::Init()
{
    auto init = initState_.TryBeginInit();
    if (!init)
    {
        ESP_LOGW(TAG, "Already initialized or initializing");
        return;
    }

    // Shared I2C bus (GT911 touch + AHT20 sensor on SDA17/SCL18).
    i2c_master_bus_config_t cfg = {};
    cfg.i2c_port = I2C_NUM_0;
    cfg.sda_io_num = (gpio_num_t)BoardConfig::I2C_PIN_SDA;
    cfg.scl_io_num = (gpio_num_t)BoardConfig::I2C_PIN_SCL;
    cfg.clk_source = I2C_CLK_SRC_DEFAULT;
    cfg.glitch_ignore_cnt = 7;
    cfg.flags.enable_internal_pullup = true;

    esp_err_t err = i2c_new_master_bus(&cfg, &i2cBus_);
    if (err != ESP_OK)
    {
        // Not boot-critical: the board still runs (WiFi/web UI) without I2C.
        ESP_LOGE(TAG, "I2C bus create failed: %s", esp_err_to_name(err));
        i2cBus_ = nullptr;
    }

    if (i2cBus_ && ambientSensor_.Init(i2cBus_))
    {
        float celsius = 0;
        if (ambientSensor_.ReadTemperature(celsius))
            ESP_LOGI(TAG, "AHT20 ambient: %.1f degC", celsius);
    }
    else
    {
        ESP_LOGE(TAG, "AHT20 init failed (sensor unavailable)");
    }

    init.SetReady();
    ESP_LOGI(TAG, "Initialized");
}
```

- [ ] **Step 4: Write board.cmake**

```cmake
# ──────────────────────────────────────────────────────────────
# Board fragment: DIYLESS OpenTherm Thermostat 3 (hw rev v3.3)
#   ESP32-S3 (QFN56, 8 MB flash, 8 MB octal PSRAM) · 4.0" 480x480 IPS
#   ST7701S · GT911 capacitive touch · AHT20 on the shared I2C bus ·
#   STM32L051 co-processor (owns the OpenTherm PHY) · no user LED.
#
# Build with:  idf.py -B build_diyless -DBOARD=diyless_thermostat_3 build
# (fresh dir first: idf.py -B build_diyless -DBOARD=diyless_thermostat_3 set-target esp32s3)
#
# Flash/PSRAM/console config lives in this folder's sdkconfig.defaults.
# Component deps are NOT set here — managed deps go in main/idf_component.yml,
# IDF built-ins in COMPONENT_REQUIRES (see note in main/CMakeLists.txt).
# ──────────────────────────────────────────────────────────────

list(APPEND BOARD_SOURCES "${CMAKE_CURRENT_LIST_DIR}/Board.cpp")
```

- [ ] **Step 5: Write sdkconfig.defaults (board overlay)**

The current shared defaults are devkit-shaped (esp32, 4 MB, no PSRAM), so this overlay must carry everything S3/T3-specific:

```ini
# ──────────────────────────────────────────────────────────────
# Per-board sdkconfig overlay — DIYLESS Thermostat 3 (hw v3.3).
# Composed after the shared sdkconfig.defaults (root CMakeLists) and wins
# on conflicts. Target: ESP32-S3 (build dir configured with set-target esp32s3).
# ──────────────────────────────────────────────────────────────

# 8 MB flash with our 8 MB partition layout (two 3 MB OTA slots + www).
CONFIG_ESPTOOLPY_FLASHSIZE_8MB=y
CONFIG_PARTITION_TABLE_CUSTOM_FILENAME="partitions_8mb.csv"

# 8 MB embedded octal PSRAM @ 80 MHz.
CONFIG_SPIRAM=y
CONFIG_SPIRAM_MODE_OCT=y
CONFIG_SPIRAM_SPEED_80M=y

# 240 MHz CPU.
CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ_240=y

# Console on USB-Serial/JTAG, NOT UART0. UART0's default pins are repurposed
# on this board: GPIO43 is the LCD reset and GPIO44 the STM32 boot pin — a
# UART0 console would bit-bang log traffic onto them.
CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG=y
```

- [ ] **Step 6: Commit**

```bash
git add main/hardware/boards/diyless_thermostat_3
git commit -m "Add DIYLESS Thermostat 3 board target (minimal boot: I2C + AHT20, no LED)

Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>"
```

### Task 3: Build both variants + document the build

**Files:**
- Modify: `CLAUDE.md` (repo root — Build commands section)

**Interfaces:**
- Consumes: board folder from Task 2.
- Produces: green `build/` (devkit, esp32) and `build_diyless/` (esp32s3); updated build docs.

- [ ] **Step 1: Devkit regression build**

Run (PowerShell, repo root):

```powershell
$env:PYTHONUTF8 = "1"; $env:PYTHONIOENCODING = "utf-8"
. "C:\Espressif\tools\Microsoft.v6.0.PowerShell_profile.ps1"
idf.py build
```

Expected: `Project build complete.` — the new board folder must not affect the default variant.

- [ ] **Step 2: Diyless variant — fresh dir, set target, build**

```powershell
idf.py -B build_diyless -DBOARD=diyless_thermostat_3 set-target esp32s3
idf.py -B build_diyless -DBOARD=diyless_thermostat_3 build
```

Expected: configure log prints `Building for BOARD=diyless_thermostat_3`; build ends `Project build complete.`; the size summary references the 3 MB app partition (0x300000). If CMake errors "Unknown BOARD", the folder name/board.cmake is wrong. If the partition table tool complains the table exceeds flash, `sdkconfig.defaults` didn't apply — delete `build_diyless/` and reconfigure.

- [ ] **Step 3: Update repo CLAUDE.md build docs**

In the `## Build commands` firmware block, after the existing `idf.py -DBOARD=<name> build` line, add:

```markdown
The DIYLESS Thermostat 3 target is ESP32-S3 and must use its own build dir
(the default `build/` stays esp32/devkit):

```bash
idf.py -B build_diyless -DBOARD=diyless_thermostat_3 set-target esp32s3   # once
idf.py -B build_diyless -DBOARD=diyless_thermostat_3 build
idf.py -B build_diyless -p <PORT> flash monitor    # console is on USB-Serial/JTAG
```
```

- [ ] **Step 4: Commit**

```bash
git add CLAUDE.md
git commit -m "Document diyless_thermostat_3 build (separate esp32s3 build dir)

Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>"
```

### Task 4: Hardware smoke test + push

**Files:** none (verification + push only).

**Interfaces:**
- Consumes: `build_diyless/` from Task 3. Requires the physical T3 on a COM port (its USB-Serial/JTAG enumerates as its own COM device — ask the user which port if not obvious; do NOT assume COM30, that's the gateway).

- [ ] **Step 1: Flash and watch the console**

```powershell
idf.py -B build_diyless -p <PORT> flash monitor
```

Expected on the monitor (USB-Serial/JTAG): boot banner, `Board: AHT20 ambient: <plausible room temp> degC`, `Board: Initialized`, every manager `Init` line, `main: All managers initialized, firmware confirmed valid`. No reboot loop (rollback watchdog would reboot if app crashed).

- [ ] **Step 2: Network + web UI check**

With no stored WiFi credentials the AP fallback appears (SSID per NetworkManager default, currently Strux-branded). Connect to it (or configure STA creds via the console) and open the web UI in a browser: login page loads, login works, Settings/Firmware/Console pages render.

- [ ] **Step 3: Push the branch**

```bash
git push -u origin feature/diyless-board-target
```

Expected: `* [new branch] feature/diyless-board-target`.

- [ ] **Step 4: Report** — summarize console output + web UI result to the user; merge decision (to `main`) is theirs via finishing-a-development-branch.
